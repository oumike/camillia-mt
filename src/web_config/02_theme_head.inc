// SPDX-License-Identifier: GPL-3.0-or-later
// Part of web_config.cpp: theme colours and the shared page head.
// Included in order from web_config.cpp as one translation unit; not built on its own.

// Widened back to 8 bits per channel for the browser. The stored value is
// RGB565, so this is lossy in the direction that already happened — round-trip
// it and you get the same 565 back, which is what makes the color inputs
// stable when a theme is reopened.
static uint32_t rgb565To888(uint16_t c) {
    const uint32_t r = (((c >> 11) & 0x1F) * 255U) / 31U;
    const uint32_t g = (((c >> 5) & 0x3F) * 255U) / 63U;
    const uint32_t b = ((c & 0x1F) * 255U) / 31U;
    return (r << 16) | (g << 8) | b;
}

static uint16_t rgb888To565(uint32_t rgb) {
    return (uint16_t)((((rgb >> 16) & 0xF8) << 8)
                    | (((rgb >> 8) & 0xFC) << 3)
                    | ((rgb & 0xFF) >> 3));
}

// Accepts "#RRGGBB" or "RRGGBB" — the color input posts the first, a person
// typing a hex code by hand may post either.
static bool parseHexColor(const String &s, uint16_t &out) {
    const char *p = s.c_str();
    if (*p == '#') p++;
    if (strlen(p) != 6) return false;
    uint32_t v = 0;
    for (int i = 0; i < 6; i++) {
        const char c = p[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F') d = 10 + (c - 'A');
        else return false;
        v = (v << 4) | (uint32_t)d;
    }
    out = rgb888To565(v);
    return true;
}

// Custom themes continue the preset numbering: kThemePresetCount + slot, so a
// slot keeps its number whether or not the slots before it are filled. The
// picker only draws cards for slots that exist, so the gaps never show.
static constexpr uint8_t kThemePresetCustomBase = kThemePresetCount;
static constexpr uint8_t kThemePresetTotal =
    (uint8_t)(kThemePresetCount + UI_CUSTOM_THEME_SLOTS);

static uint8_t themePresetFromConfig(const RhinoConfig &cfg) {
    if (uiThemeIsCustom(cfg.uiTheme)) {
        return (uint8_t)(kThemePresetCustomBase + uiThemeCustomSlot(cfg.uiTheme));
    }

    uint8_t themeBase = (uint8_t)constrain((int)cfg.uiTheme, 0, UI_THEME_COUNT - 1);
    if (themeBase == UI_THEME_CAMELLIA_BLACK) {
        return (uint8_t)(kThemePresetCount - 1);
    }

    uint8_t mode = (cfg.uiMode == UI_MODE_LIGHT) ? UI_MODE_LIGHT : UI_MODE_DARK;
    return (uint8_t)constrain((int)(themeBase * 2 + mode), 0, (int)kThemePresetCount - 1);
}

static void themePresetToConfig(uint8_t preset, uint8_t &themeOut, uint8_t &modeOut) {
    preset = (uint8_t)constrain((int)preset, 0, (int)kThemePresetTotal - 1);
    if (preset >= kThemePresetCustomBase) {
        const int slot = preset - kThemePresetCustomBase;
        const UiCustomTheme *ct = uiCustomThemeGet(slot);
        if (ct) {
            themeOut = uiThemeFromCustomSlot(slot);
            modeOut = ct->mode;   // the theme carries its own mode
            return;
        }
        // Deleted between the page render and the save. Leave the config alone
        // rather than selecting a theme that is not there.
        return;
    }
    if (preset == (uint8_t)(kThemePresetCount - 1)) {
        themeOut = UI_THEME_CAMELLIA_BLACK;
        modeOut = UI_MODE_DARK;
        return;
    }

    themeOut = (uint8_t)constrain((int)(preset / 2), 0, UI_THEME_COUNT - 1);
    modeOut = (uint8_t)((preset & 1) ? UI_MODE_LIGHT : UI_MODE_DARK);
    if (uiThemeForcesDark(themeOut)) modeOut = UI_MODE_DARK;
}

// The remaining constants below are the config pane's fat static blobs, pulled
// out of the build-a-String path. Appending a multi-KB literal forces a
// contiguous allocation that big; in AP mode, where the largest free block is
// ~13 KB, that is what made the page fail to render. Streamed with
// sendContent_P they cost nothing, so the same pane serves both modes.

static const char kPresetJs[] =
        "<script>"
        // Region band edges {start,end MHz, power dBm} — mirror kRegions.
        "var R={'US':{s:902,e:928,p:22},'EU_433':{s:433,e:434,p:10},'EU_868':{s:869.4,e:869.65,p:22},"
        // sp = spacing, pd = padding, os = pinned 1-based slot. Absent on every
        // classic region and read as 0 there, which is what they all use.
        "'EU_866':{s:865.6,e:867.6,p:22,sp:0.4,pd:0.0375},"
        "'EU_N_868':{s:869.4,e:869.65,p:22,pd:0.0104,os:1},"
        "'CN':{s:470,e:510,p:19},'JP':{s:920.5,e:923.5,p:13},'ANZ':{s:915,e:928,p:22},'ANZ_433':{s:433.05,e:434.79,p:14},"
        "'RU':{s:868.7,e:869.2,p:20},'KR':{s:920,e:923,p:22},'TW':{s:920,e:925,p:22},'IN':{s:865,e:867,p:22},"
        "'NZ_865':{s:864,e:868,p:22},'TH':{s:920,e:925,p:16},'UA_433':{s:433,e:434.7,p:10},'UA_868':{s:868,e:868.6,p:14},"
        "'MY_433':{s:433,e:435,p:20},'MY_919':{s:919,e:924,p:22},'SG_923':{s:917,e:925,p:20},'PH_433':{s:433,e:434.7,p:10},"
        "'PH_868':{s:868,e:869.4,p:14},'PH_915':{s:915,e:918,p:22},'KZ_433':{s:433.075,e:434.775,p:10},'KZ_863':{s:863,e:868,p:22},"
        "'NP_865':{s:865,e:868,p:22},'BR_902':{s:902,e:907.5,p:22},'LORA_24':{s:2400,e:2483.5,p:10}"
#if FEATURE_LICENSED_MODE
        // Amateur-band rows of kRegions; the select offers them only while licensed.
        ",'ITU1_2M':{s:144,e:146,p:22,pd:0.0022,os:26},'ITU2_2M':{s:144,e:148,p:22,pd:0.0022,os:51},"
        "'ITU3_2M':{s:144,e:148,p:22,pd:0.0022,os:33},'ITU2_125CM':{s:220,e:225,p:22,pd:0.01875,os:37},"
        "'ITU1_70CM':{s:430,e:440,p:22,pd:0.01875,os:37},'ITU2_70CM':{s:420,e:450,p:22,pd:0.01875,os:137},"
        "'ITU3_70CM':{s:430,e:450,p:22,pd:0.01875,os:37}"
#endif
        "};"
        // Preset params {bw kHz, sf, cr, channel name for frequency-slot hash}.
        "var P={'Long Fast':{bw:250,sf:11,cr:5,cn:'LongFast'},'Long Moderate':{bw:125,sf:11,cr:8,cn:'LongMod'},"
        "'Long Slow':{bw:125,sf:12,cr:8,cn:'LongSlow'},'Long Turbo':{bw:500,sf:11,cr:8,cn:'LongTurbo'},"
        "'Medium Fast':{bw:250,sf:9,cr:5,cn:'MediumFast'},'Medium Slow':{bw:250,sf:10,cr:5,cn:'MediumSlow'},"
        "'Short Fast':{bw:250,sf:7,cr:5,cn:'ShortFast'},'Short Slow':{bw:250,sf:8,cr:5,cn:'ShortSlow'},'Short Turbo':{bw:500,sf:7,cr:5,cn:'ShortTurbo'},"
        "'Medium Turbo':{bw:500,sf:9,cr:5,cn:'MediumTurbo'},"
        "'Lite Fast':{bw:125,sf:9,cr:5,cn:'LiteFast'},'Lite Slow':{bw:125,sf:10,cr:5,cn:'LiteSlow'},"
        "'Narrow Fast':{bw:62.5,sf:7,cr:6,cn:'NarrowFast'},'Narrow Slow':{bw:62.5,sf:8,cr:6,cn:'NarrowSlow'},"
        // Kept in the table on every board even where the options are hidden:
        // this drives the readout, and a config imported from a board that does
        // have the radio should still render rather than blank the panel.
        "'Tiny Fast':{bw:15.6,sf:7,cr:5,cn:'TinyFast'},'Tiny Slow':{bw:15.6,sf:8,cr:6,cn:'TinySlow'}};"
        // djb2 hash + Meshtastic frequency-slot calc, matching regionSlotFreq().
        "function djb2(t){var h=5381;for(var i=0;i<t.length;i++)h=((h*33)+t.charCodeAt(i))>>>0;return h;}"
        // Mirrors regionSlotCount()/regionSlotFreqNum(). Math.round, not floor: the
        // readout has to name the frequency the radio will actually tune, and the
        // two disagree wherever the band is not a whole number of channels wide.
        "function sw(r,p){return (r.sp||0)+2*(r.pd||0)+p.bw/1000;}"
        "function nSlot(r,p){var n=Math.round((r.e-r.s+(r.sp||0))/sw(r,p));return n<1?1:n;}"
        "function slotN(r,p,i){var n=nSlot(r,p);if(i>=n)i=n-1;return r.s+p.bw/2000+(r.pd||0)+i*sw(r,p);}"
        // Primary channel name, which is what a custom slot-0 setup hashes.
        // Saving with Custom selected renames a still-default channel 0 to
        // "Custom" (syncPrimaryChannelName), so a preset name in that field is
        // not what the device will end up hashing — predict the rename here or
        // the readout advertises a frequency the radio never tunes to.
        "function isDefName(n){if(!n||n=='Custom')return 1;"
          "for(var k in P)if(P[k].cn==n)return 1;return 0;}"
        "function cn0(){var e=document.querySelector('[name=ch0_name]');"
          "var n=e?e.value.trim():'';return isDefName(n)?'Custom':n;}"
        "function applyPreset(){"
          "var r=R[document.getElementById('sel-rgn').value];"
          "var pv=document.getElementById('sel-pst').value;"
          "var cust=(pv=='Custom');"
          "var cb=document.getElementById('cust-blk');"
          "if(cb)cb.style.display=cust?'':'none';"
          "var p,slot=0;"
          "if(cust){"
            "var c=+document.getElementById('sel-cbw').value;"
            "p={bw:c==16?15.6:c==31?31.25:c==62?62.5:c,sf:+document.getElementById('sel-csf').value,"
              "cr:+document.getElementById('sel-ccr').value,cn:cn0()};"
            "slot=+document.getElementById('in-cslot').value||0;"
          "}else p=P[pv];"
          "if(!r||!p)return;"
          "var n=nSlot(r,p);"
          "var si=slot>0?(slot>n?n:slot):(r.os?r.os:(djb2(p.cn)%n)+1);"
          "var f=slotN(r,p,si-1);"
          "document.getElementById('d-freq').textContent=f.toFixed(3)+' MHz'"
            "+(cust?' (slot '+si+' of '+n+')':'');"
          "document.getElementById('d-bw').textContent=p.bw+' kHz';"
          "document.getElementById('d-sf').textContent='SF'+p.sf;"
          "document.getElementById('d-cr').textContent='4/'+p.cr;"
          "document.querySelector('[name=pwr]').value=r.p;"
        "}"
        "document.addEventListener('DOMContentLoaded',applyPreset);"
        "</script>";

static const char kLoraReadout[] =
        "<div class='row2' style='align-items:center'>"
        "<label>Frequency<span id='d-freq' style='display:inline-block;padding:.25em .5em;"
        "background:var(--panel-2);color:var(--text);border-radius:4px;min-width:8em;text-align:center'>—</span></label>"
        "<label>Bandwidth<span id='d-bw' style='display:inline-block;padding:.25em .5em;"
        "background:var(--panel-2);color:var(--text);border-radius:4px;min-width:6em;text-align:center'>—</span></label>"
        "</div><div class='row2' style='align-items:center'>"
        "<label>Spreading Factor<span id='d-sf' style='display:inline-block;padding:.25em .5em;"
        "background:var(--panel-2);color:var(--text);border-radius:4px;min-width:5em;text-align:center'>—</span></label>"
        "<label>Coding Rate<span id='d-cr' style='display:inline-block;padding:.25em .5em;"
        "background:var(--panel-2);color:var(--text);border-radius:4px;min-width:5em;text-align:center'>—</span></label>"
        "</div>";

#if HAS_WEB_FILES
// The Files tab: markup, styles and script in one constant, sent only when the
// tab is switched on, so a device with it off pays nothing for it. The caller
// wraps it -- a tab panel on the full page, a plain block on the Cardputer's
// lite page, whose head defines none of the colour variables (hence the
// fallbacks). Rows are
// built with DOM calls rather than HTML strings, so a file name is only ever
// text -- never markup, never a quote that breaks an onclick.
static const char kFilesPane[] =
        "<div class='tab-pane-center'>"
        "<style>"
            "#tab-files .fs-bar{display:flex;flex-wrap:wrap;gap:.4em;align-items:center;margin:.8em 0}"
            "#tab-files .fs-bar button,#tab-files .fs-act button{margin:0;padding:.35em .9em;font-size:.88em}"
            "#tab-files .fs-act button{padding:.25em .6em;margin-left:.3em;background:var(--panel-2,transparent);"
                "color:var(--text,inherit);border:1px solid var(--line,#999)}"
            "#tab-files .fs-act button.fs-del{color:#ff8d8d}"
            "#tab-files .fs-crumbs{font-family:monospace;word-break:break-all}"
            "#tab-files .fs-crumbs a{cursor:pointer;color:var(--accent,#2a7ae2)}"
            "#tab-files table{width:100%;border-collapse:collapse;font-size:.92em}"
            "#tab-files td{padding:.35em .4em;border-bottom:1px solid var(--line,#999);vertical-align:middle}"
            "#tab-files td.fs-size{text-align:right;white-space:nowrap;color:var(--text-dim,#888)}"
            "#tab-files td.fs-act{text-align:right;white-space:nowrap}"
            "#tab-files .fs-name{word-break:break-all}"
            "#tab-files .fs-name a{cursor:pointer;color:var(--accent,#2a7ae2);font-weight:600}"
            "#tab-files progress{width:100%;display:none}"
        "</style>"
        "<h3 style='margin-top:1.2em'>Files</h3>"
        "<p id='fs-info' style='font-size:.85em;color:var(--text-dim,#888);margin:.2em 0'></p>"
        "<div class='fs-crumbs' id='fs-crumbs'></div>"
        "<div class='fs-bar'>"
            "<button type='button' onclick=\"document.getElementById('fs-file').click()\">Upload</button>"
            "<button type='button' onclick='fsMkdir()'>New folder</button>"
            "<button type='button' onclick='filesLoad(fsCur)'>Refresh</button>"
            "<input type='file' id='fs-file' multiple style='display:none' onchange='fsUpload(this)'>"
        "</div>"
        "<progress id='fs-prog' max='100' value='0'></progress>"
        "<p id='fs-status' style='font-size:.88em;min-height:1.2em;margin:.3em 0'></p>"
        "<table><tbody id='fs-rows'></tbody></table>"
        "</div>"
        "<script>"
        "var fsCur='/',fsLoaded=false,fsNames={};"
        "function fsJoin(d,n){return d==='/'?'/'+n:d+'/'+n;}"
        "function fsSize(b){"
            "if(b<1024)return b+' B';"
            "if(b<1048576)return (b/1024).toFixed(1)+' KB';"
            "if(b<1073741824)return (b/1048576).toFixed(1)+' MB';"
            "return (b/1073741824).toFixed(2)+' GB';"
        "}"
        "function fsStatus(m,err){"
            "var el=document.getElementById('fs-status');"
            "el.textContent=m||'';el.style.color=err?'#ff8d8d':'';"
        "}"
        "function filesOpen(){if(!fsLoaded)filesLoad(fsCur);}"
        "function fsEl(tag,text,cls){"
            "var e=document.createElement(tag);"
            "if(text!==undefined)e.textContent=text;"
            "if(cls)e.className=cls;"
            "return e;"
        "}"
        "function fsBtn(text,fn,cls){"
            "var b=fsEl('button',text,cls);b.type='button';b.onclick=fn;return b;"
        "}"
        "function fsCrumbs(p){"
            "var c=document.getElementById('fs-crumbs');c.textContent='';"
            "var parts=p.split('/').filter(function(x){return x;});"
            "var a=fsEl('a','/');a.onclick=function(){filesLoad('/');};c.appendChild(a);"
            "var acc='';"
            "parts.forEach(function(seg,i){"
                "acc+='/'+seg;"
                "var to=acc;"
                "if(i>0)c.appendChild(document.createTextNode('/'));"
                "var l=fsEl('a',seg);l.onclick=function(){filesLoad(to);};c.appendChild(l);"
            "});"
        "}"
        "function filesLoad(p){"
            "fsStatus('Loading...');"
            "fetch('/fs-list?path='+encodeURIComponent(p),{cache:'no-store'})"
            ".then(function(r){return r.json();})"
            ".then(function(j){"
                "if(!j.ok)throw new Error(j.error||'failed');"
                "fsCur=j.path;fsLoaded=true;fsNames={};"
                "document.getElementById('fs-info').textContent="
                    "'Storage: '+j.fs+(j.total?' ('+fsSize(j.total)+')':'');"
                "fsCrumbs(j.path);"
                "var rows=document.getElementById('fs-rows');rows.textContent='';"
                "j.entries.sort(function(a,b){"
                    "if(a.d!==b.d)return b.d-a.d;"
                    "return a.n.toLowerCase()<b.n.toLowerCase()?-1:1;"
                "});"
                "if(j.path!=='/'){"
                    "var up=fsEl('tr'),td=fsEl('td',undefined,'fs-name');td.colSpan=3;"
                    "var ua=fsEl('a','..');ua.onclick=function(){"
                        "var i=fsCur.lastIndexOf('/');filesLoad(i<=0?'/':fsCur.substring(0,i));};"
                    "td.appendChild(ua);up.appendChild(td);rows.appendChild(up);"
                "}"
                "j.entries.forEach(function(e){"
                    "var full=fsJoin(j.path,e.n);fsNames[e.n]=e.d;"
                    "var tr=fsEl('tr'),name=fsEl('td',undefined,'fs-name');"
                    "if(e.d){"
                        "var a=fsEl('a',e.n+'/');a.onclick=function(){filesLoad(full);};name.appendChild(a);"
                    "}else{"
                        "name.textContent=e.n;"
                    "}"
                    "tr.appendChild(name);"
                    "tr.appendChild(fsEl('td',e.d?'':fsSize(e.s),'fs-size'));"
                    "var act=fsEl('td',undefined,'fs-act');"
                    "if(!e.d){"
                        "act.appendChild(fsBtn('Download',function(){"
                            "location.href='/fs-get?path='+encodeURIComponent(full);}));"
                        "act.appendChild(fsBtn('Copy',function(){fsCopy(full);}));"
                    "}"
                    "act.appendChild(fsBtn('Delete',function(){fsDelete(full,e.d);},'fs-del'));"
                    "tr.appendChild(act);rows.appendChild(tr);"
                "});"
                "if(!j.entries.length){"
                    "var er=fsEl('tr'),ed=fsEl('td','Empty folder');ed.colSpan=3;"
                    "ed.style.color='var(--text-dim,#888)';er.appendChild(ed);rows.appendChild(er);"
                "}"
                "fsStatus(j.truncated?'Showing the first '+j.entries.length+' entries only.':'');"
            "})"
            ".catch(function(e){"
                "fsStatus(e.message==='unauthorized'"
                    "?'Files are switched off on the device (Config > Web Files), or you are logged out.'"
                    ":'Could not list folder: '+e.message,true);"
            "});"
        "}"
        // POST, then reload the folder. Resolves to the reply so a caller can
        // react to a specific error (exists -> ask to overwrite).
        "function fsPost(url){"
            "return fetch(url,{method:'POST'}).then(function(r){return r.json();});"
        "}"
        "function fsDelete(path,isDir){"
            "var q=isDir?'Delete the folder '+path+' and everything in it?':'Delete '+path+'?';"
            "if(!confirm(q))return;"
            "fsStatus('Deleting...');"
            "fsPost('/fs-delete?path='+encodeURIComponent(path)).then(function(j){"
                "if(!j.ok)throw new Error(j.error);"
                "fsStatus('Deleted '+path);filesLoad(fsCur);"
            "}).catch(function(e){fsStatus('Delete failed: '+e.message,true);});"
        "}"
        "function fsMkdir(){"
            "var n=prompt('New folder name');"
            "if(!n)return;"
            "if(n.indexOf('/')>=0||n==='.'||n==='..'){fsStatus('Folder names cannot contain /',true);return;}"
            "fsPost('/fs-mkdir?path='+encodeURIComponent(fsJoin(fsCur,n))).then(function(j){"
                "if(!j.ok)throw new Error(j.error);"
                "fsStatus('Created '+n);filesLoad(fsCur);"
            "}).catch(function(e){fsStatus('New folder failed: '+e.message,true);});"
        "}"
        "function fsCopy(from){"
            "var to=prompt('Copy '+from+' to (full path):',from);"
            "if(!to||to===from)return;"
            "var go=function(ow){"
                "fsStatus('Copying...');"
                "fsPost('/fs-copy?from='+encodeURIComponent(from)+'&to='+encodeURIComponent(to)"
                    "+(ow?'&overwrite=1':'')).then(function(j){"
                    "if(!j.ok&&j.error==='exists'&&!ow){"
                        "if(confirm(to+' already exists. Replace it?'))go(true);else fsStatus('');"
                        "return;"
                    "}"
                    "if(!j.ok)throw new Error(j.error);"
                    "fsStatus('Copied to '+to);filesLoad(fsCur);"
                "}).catch(function(e){fsStatus('Copy failed: '+e.message,true);});"
            "};"
            "go(false);"
        "}"
        // One file per request, one request at a time: the device handles a
        // single connection, and a failed file should not take the rest down.
        "function fsUpload(input){"
            "var files=Array.prototype.slice.call(input.files);input.value='';"
            "var dir=fsCur,prog=document.getElementById('fs-prog'),done=0,bad=0;"
            "var next=function(){"
                "if(!files.length){"
                    "prog.style.display='none';"
                    "if(done&&!bad)fsStatus('Uploaded '+done+' file'+(done===1?'':'s'));"
                    "filesLoad(dir);return;"
                "}"
                "var f=files.shift();"
                "if(fsNames[f.name]===1){bad++;fsStatus(f.name+': a folder has that name',true);next();return;}"
                "var ow=fsNames[f.name]===0;"
                "if(ow&&!confirm(f.name+' already exists. Replace it?')){next();return;}"
                "var fd=new FormData();fd.append('file',f,f.name);"
                "var x=new XMLHttpRequest();"
                "x.open('POST','/fs-put?dir='+encodeURIComponent(dir)+(ow?'&overwrite=1':''));"
                "x.upload.onprogress=function(ev){"
                    "if(ev.lengthComputable)prog.value=Math.round(ev.loaded*100/ev.total);"
                "};"
                "x.onload=function(){"
                    "var j={};try{j=JSON.parse(x.responseText);}catch(e){}"
                    "if(j.ok){done++;}else{bad++;fsStatus(f.name+': '+(j.error||('HTTP '+x.status)),true);}"
                    "next();"
                "};"
                "x.onerror=function(){bad++;fsStatus(f.name+': connection lost',true);next();};"
                "prog.value=0;prog.style.display='block';"
                "fsStatus('Uploading '+f.name+' ('+fsSize(f.size)+')...');"
                "x.send(fd);"
            "};"
            "next();"
        "}"
        "</script>";
#endif

#if HAS_ADMIN_TERMINAL
// Emitted at body level, after every tab panel has closed -- NOT inside one.
// .tab-panel is display:none unless it is the active tab, and a fixed overlay
// inside a hidden ancestor does not render however much .open it is given. The
// terminal is opened from Utilities, so parenting it to the Config panel is
// exactly the case that fails.
static const char kAdminModal[] =
        "<div class='modal-back' id='adminModal' onclick='if(event.target===this)adminClose()'>"
        // Two cards in a row, but only once there are two. On its own the
        // terminal centres like every other modal on this page -- pinning it
        // left made it look pushed aside by a card that was not there.
        //
        // When `help` opens the second card the row switches to flex-start, so
        // the terminal steps left and the table appears beside it rather than
        // both of them sliding to make room. Wrapping rather than a fixed pair,
        // so a phone stacks them instead of squeezing both.
        "<div id='admin-row' style='display:flex;gap:.8em;align-items:flex-start;"
            "flex-wrap:wrap;justify-content:center;width:100%;padding:0 2vw'>"
        "<div class='modal-card' style='width:min(42em,92vw);margin:0'>"
        "<div style='display:flex;align-items:center;gap:.6em;margin:.1em 0 .6em'>"
        "<h3 id='admin-title' style='margin:0;flex:1;min-width:0;overflow:hidden;"
            "text-overflow:ellipsis;white-space:nowrap'>Remote Admin</h3>"
        // The same panel `help` opens, reachable without knowing to type it.
        // Idempotent, so pressing it with the panel already up just refetches.
        "<button type='button' onclick='adminHelpShow()'"
            " title='Show the command list'>Help</button>"
        "</div>"
        "<div id='admin-log' style='height:22em;overflow-y:auto;background:var(--panel-2);"
            "border:1px solid var(--line);border-radius:6px;padding:.5em;"
            "font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:.8em;"
            "white-space:pre-wrap;word-break:break-word'></div>"
        "<div style='display:flex;gap:.4em;margin-top:.6em'>"
            "<input id='admin-line' placeholder='help' autocomplete='off' style='flex:1'"
                " onkeydown='if(event.key===\"Enter\"){event.preventDefault();adminSend();}'>"
            "<button type='button' onclick='adminSend()'>Send</button>"
            "<button type='button' onclick='adminClose()'>Close</button>"
        "</div></div>"
        // The help card. display:none until `help` is typed, so the terminal has
        // the full width until there is something to put beside it.
        "<div class='modal-card' id='admin-help-card' style='width:min(30em,92vw);"
            "margin:0;display:none'>"
        "<h3>Commands</h3>"
        "<pre id='admin-help' style='margin:0;font-size:.74em;line-height:1.45;"
            "white-space:pre-wrap;word-break:break-word'></pre>"
        "<button type='button' style='margin-top:.6em' onclick='adminHelpHide()'>"
            "Hide</button>"
        "</div>"
        "</div></div>";
#endif

static const char kFontModal[] =
        "<div class='modal-back' id='fsModal' onclick='if(event.target===this)fsClose()'>"
        "<div class='modal-card'><h3>Font Size</h3>"
        "<button type='button' class='modal-opt' onclick='fsPick(0,\"Small\")'>"
            "<b>Small</b><span>Compact chat &amp; DM text</span></button>"
        "<button type='button' class='modal-opt' onclick='fsPick(1,\"Medium\")'>"
            "<b>Medium</b><span>Default chat &amp; DM text</span></button>"
        "<button type='button' class='modal-opt' onclick='fsPick(2,\"Large\")'>"
            "<b>Large</b><span>Larger chat &amp; DM text</span></button>"
        "<button type='button' class='modal-opt' onclick='fsPick(3,\"Extra Large\")'>"
            "<b>Extra Large</b><span>Largest chat &amp; DM text</span></button>"
        "</div></div>"
        "<script>"
        "function fsMark(v){var m=document.querySelectorAll('#fsModal .modal-opt');"
        "for(var i=0;i<m.length;i++)m[i].classList.toggle('sel',i==v);}"
        "function fsOpen(){fsMark(+document.getElementById('fsInput').value);"
        "document.getElementById('fsModal').classList.add('open');}"
        "function fsClose(){document.getElementById('fsModal').classList.remove('open');}"
        "function fsPick(v,l){document.getElementById('fsInput').value=v;"
        "document.getElementById('fsBtn').textContent=l;fsClose();}"
        "</script>";

// Swatch picker: each theme preset renders as a clickable card showing a
// three-color preview (background, panel, accent). Cards are built in JS from
// the same palette table used for live preview, keyed by preset.
static const char kThemePicker[] =
        "<style>"
        ".theme-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));"
        "gap:8px;margin:.2em 0 .4em}"
        ".theme-card{display:flex;align-items:center;gap:8px;padding:7px 9px;cursor:pointer;"
        "border:1px solid var(--line);border-radius:8px;background:var(--panel);color:var(--text)}"
        ".theme-card.sel{border-color:var(--accent);border-width:2px;padding:6px 8px}"
        ".theme-sws{display:flex;gap:3px;flex:0 0 auto}"
        ".theme-sw{width:16px;height:16px;border-radius:3px;border:1px solid rgba(0,0,0,.25)}"
        ".theme-name{font-size:.86em;line-height:1.15}"
        ".theme-filter{display:flex;align-items:center;gap:8px;margin:.2em 0 .1em}"
        ".theme-filter input{flex:1 1 auto;margin:0}"
        ".theme-filter span{font-size:.8em;color:var(--text-dim);white-space:nowrap}"
        // Custom cards carry two corner buttons, so they need room underneath
        // the row and a positioning context for them.
        ".theme-card.cust{position:relative;padding-bottom:15px}"
        ".theme-card.cust.sel{padding-bottom:14px}"
        ".theme-btn-cnr{position:absolute;bottom:2px;width:15px;height:15px;line-height:13px;"
        "text-align:center;border-radius:4px;font-size:11px;font-weight:700;cursor:pointer;"
        "border:1px solid rgba(0,0,0,.3);padding:0}"
        ".theme-del{right:3px;background:#c62839;color:#fff}"
        ".theme-edit{right:21px;background:var(--panel-2);color:var(--text)}"
        ".theme-card.new{justify-content:center;border-style:dashed;font-weight:600}"
        // The builder. A fixed overlay rather than a <dialog>, which older
        // mobile browsers on the far end of a device AP still do not have.
        ".tb-back{position:fixed;inset:0;background:rgba(0,0,0,.55);display:none;z-index:50}"
        ".tb-back.on{display:block}"
        ".tb-box{position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);"
        "width:min(340px,92vw);max-height:92vh;overflow:auto;background:var(--panel);color:var(--text);"
        "border:1px solid var(--line);border-radius:10px;padding:14px}"
        ".tb-box h3{margin:0 0 .5em}"
        ".tb-row{display:flex;align-items:center;gap:8px;margin:.45em 0}"
        ".tb-row label{flex:1 1 auto;font-size:.88em;margin:0}"
        ".tb-row input[type=color]{width:38px;height:26px;padding:0;flex:0 0 auto}"
        ".tb-row input[type=text]{width:96px;flex:0 0 auto;font-family:monospace}"
        ".tb-modes{display:flex;gap:14px;align-items:center;margin:.5em 0}"
        ".tb-modes label{display:flex;align-items:center;gap:5px;font-size:.88em;margin:0}"
        ".tb-modes input{width:auto;margin:0}"
        ".tb-prev{border:1px solid var(--line);border-radius:8px;padding:9px;margin:.6em 0}"
        ".tb-prev-t{font-weight:700;font-size:.9em}"
        ".tb-prev-d{font-size:.78em;opacity:.85}"
        ".tb-prev-a{display:inline-block;margin-top:6px;padding:3px 9px;border-radius:5px;font-size:.78em}"
        ".tb-msg{font-size:.8em;min-height:1.1em;margin:.3em 0}"
        ".tb-acts{display:flex;gap:8px;justify-content:flex-end;margin-top:.7em}"
        "</style>"
        "<div class='theme-filter'>"
        "<input type='text' id='themeFilter' placeholder='Filter themes' autocomplete='off'>"
        "<span id='themeCount'></span></div>"
        "<div id='themeGrid' class='theme-grid'></div>"
        "<div class='tb-back' id='tbBack'><div class='tb-box'>"
        "<h3 id='tbTitle'>New Theme</h3>"
        "<div class='tb-row'><label for='tbName'>Name</label>"
        "<input type='text' id='tbName' maxlength='15' style='width:150px' placeholder='My Theme'></div>"
        "<div class='tb-row'><label for='tbBgP'>Background</label>"
        "<input type='color' id='tbBgP'><input type='text' id='tbBg' maxlength='7'></div>"
        "<div class='tb-row'><label for='tbPnP'>Panel</label>"
        "<input type='color' id='tbPnP'><input type='text' id='tbPn' maxlength='7'></div>"
        "<div class='tb-row'><label for='tbP2P'>Panel Alt</label>"
        "<input type='color' id='tbP2P'><input type='text' id='tbP2' maxlength='7'></div>"
        "<div class='tb-row'><label for='tbAcP'>Accent</label>"
        "<input type='color' id='tbAcP'><input type='text' id='tbAc' maxlength='7'></div>"
        "<div class='tb-modes'><span style='font-size:.88em'>Mode</span>"
        "<label><input type='radio' name='tbMode' value='0' checked>Dark</label>"
        "<label><input type='radio' name='tbMode' value='1'>Light</label></div>"
        "<div class='tb-prev' id='tbPrev'>"
        "<div class='tb-prev-t' id='tbPrevT'>Preview</div>"
        "<div class='tb-prev-d' id='tbPrevD'>Dim text reads like this</div>"
        "<span class='tb-prev-a' id='tbPrevA'>Accent</span></div>"
        "<div class='tb-row'><label for='tbCode'>Import code</label>"
        "<input type='text' id='tbCode' style='width:150px;font-family:monospace' placeholder='paste hex'>"
        "<button type='button' id='tbImport'>Load</button></div>"
        "<div class='tb-msg' id='tbMsg'></div>"
        "<div class='tb-acts'><button type='button' id='tbCancel'>Cancel</button>"
        "<button type='button' id='tbSave'>Save Theme</button></div>"
        "</div></div>"
        "<script>"
        "(function(){"
        "var P={"
                        "'0':{bg:'#10141d',panel:'#1a2230',panel2:'#232d3e',line:'#4a5b73',text:'#f4f6fb',dim:'#b0b8c8',accent:'#d7869d',ink:'#ffffff'},"
                        "'1':{bg:'#f6ede9',panel:'#fff6f3',panel2:'#f4e2dc',line:'#cfb2ab',text:'#2e2220',dim:'#6f5c58',accent:'#b75a74',ink:'#ffffff'},"
                        "'2':{bg:'#091713',panel:'#102722',panel2:'#18332d',line:'#3a5f55',text:'#e8f4ef',dim:'#a5beb4',accent:'#5dbf9a',ink:'#073022'},"
                        "'3':{bg:'#eaf4ee',panel:'#f7fcf9',panel2:'#deece4',line:'#b5ccbf',text:'#1f2e25',dim:'#5f7668',accent:'#2f8f63',ink:'#ffffff'},"
                        "'4':{bg:'#1f1712',panel:'#2a2019',panel2:'#352920',line:'#655345',text:'#f3e9df',dim:'#c4b2a2',accent:'#c38a4a',ink:'#ffffff'},"
                        "'5':{bg:'#f3e9dd',panel:'#fbf4ea',panel2:'#efdfcc',line:'#c9b39a',text:'#3b2d23',dim:'#7f6a57',accent:'#a9763f',ink:'#ffffff'},"
                        "'6':{bg:'#002b36',panel:'#073642',panel2:'#0b4552',line:'#586e75',text:'#eee8d5',dim:'#93a1a1',accent:'#2aa198',ink:'#002b36'},"
                        "'7':{bg:'#fdf6e3',panel:'#eee8d5',panel2:'#e7e2cf',line:'#93a1a1',text:'#002b36',dim:'#586e75',accent:'#268bd2',ink:'#fdf6e3'},"
                        "'8':{bg:'#060f24',panel:'#12244c',panel2:'#1b3363',line:'#3b578f',text:'#f4f8ff',dim:'#b9c9e9',accent:'#ff4a58',ink:'#ffffff'},"
                        "'9':{bg:'#f3f7ff',panel:'#f8fbff',panel2:'#e6efff',line:'#a5bbe7',text:'#1f2d4d',dim:'#5d6e95',accent:'#c62839',ink:'#ffffff'},"
                        "'10':{bg:'#150009',panel:'#760031',panel2:'#8b0038',line:'#6f2d3b',text:'#fff1f1',dim:'#e5b3ba',accent:'#d51c39',ink:'#ffffff'},"
                        "'11':{bg:'#fff2f4',panel:'#fff8f9',panel2:'#ffeaed',line:'#d8a1aa',text:'#3a0a14',dim:'#7e3b49',accent:'#d51c39',ink:'#ffffff'},"
                        "'12':{bg:'#111318',panel:'#1c2128',panel2:'#252b34',line:'#4a525d',text:'#f3f6fa',dim:'#b7c0cc',accent:'#d8dde4',ink:'#080d14'},"
                        "'13':{bg:'#f3f5f7',panel:'#ffffff',panel2:'#e8ebef',line:'#c2c9d0',text:'#1e242c',dim:'#5e6876',accent:'#2e3440',ink:'#ffffff'},"
                        "'14':{bg:'#1a1230',panel:'#251a45',panel2:'#2f2258',line:'#58457f',text:'#f2eefe',dim:'#c9c0e6',accent:'#b79bff',ink:'#160f2a'},"
                        "'15':{bg:'#f5effb',panel:'#fff9ff',panel2:'#ede1f7',line:'#c7b5db',text:'#2f2440',dim:'#6d5f82',accent:'#7b5ba7',ink:'#ffffff'},"
                        "'16':{bg:'#1a2430',panel:'#253547',panel2:'#2d455b',line:'#4b6881',text:'#edf4fb',dim:'#bccbd9',accent:'#c78fcf',ink:'#25192d'},"
                        "'17':{bg:'#f6faf4',panel:'#ffffff',panel2:'#e5f0e2',line:'#bccfb7',text:'#24332f',dim:'#63756f',accent:'#8a5faf',ink:'#ffffff'},"
                        "'18':{bg:'#2a1f17',panel:'#34271e',panel2:'#403126',line:'#6a5848',text:'#f6eee6',dim:'#c9b9a8',accent:'#d9c7a3',ink:'#2a1f16'},"
                        "'19':{bg:'#faf4ea',panel:'#fffdf8',panel2:'#f1e7d5',line:'#cdbfa8',text:'#3b2f24',dim:'#7b6b57',accent:'#a8844f',ink:'#ffffff'},"
                        "'20':{bg:'#12282a',panel:'#1a3638',panel2:'#234345',line:'#4d7072',text:'#ebf7f5',dim:'#b3d0cc',accent:'#9cd8c8',ink:'#123130'},"
                        "'21':{bg:'#eef9f6',panel:'#ffffff',panel2:'#ddf1ec',line:'#b5d5cd',text:'#213531',dim:'#5f7c76',accent:'#4e9c8a',ink:'#ffffff'},"
                                                "'22':{bg:'#151f2b',panel:'#1c2a3a',panel2:'#243649',line:'#4c637c',text:'#ecf3fa',dim:'#b5c5d6',accent:'#8fb3d9',ink:'#132030'},"
                                                "'23':{bg:'#f1f7fc',panel:'#ffffff',panel2:'#dfebf6',line:'#b6c9dd',text:'#22354a',dim:'#607891',accent:'#5c86b2',ink:'#ffffff'},"
                                                "'24':{bg:'#000000',panel:'#000000',panel2:'#0a0a0a',line:'#666666',text:'#f3f6fa',dim:'#b7c0cc',accent:'#ffffff',ink:'#080d14'}"
        "};"
        "var NAMES=['Camillia Dark','Camillia Light','Evergreen Dark','Evergreen Light',"
          "'Earthy Dark','Earthy Light','Solarized Dark','Solarized Light',"
          "'Crimson Blue Dark','Crimson Blue Light','Scarlet Pop Dark','Scarlet Pop Light',"
          "'Ink Wash Dark','Ink Wash Light','Lavendar Fields Dark','Lavendar Fields Light',"
          "'Wild Flowers Dark','Wild Flowers Light','Quiet Luxury Dark','Quiet Luxury Light',"
                    "'Morning Dew Dark','Morning Dew Light','Winter Chill Dark','Winter Chill Light',"
                    "'Camillia Black'];"
        "var input=document.getElementById('themeInput');"
        // Custom themes are authored as four colors; the rest of their web
        // palette is derived here the same way applyUiThemePalette() derives it
        // on the device — same blend ratios, same mode constants — so the page
        // preview matches what the panel will actually show.
        "function hx(c){c=c.replace('#','');return [parseInt(c.substr(0,2),16),"
          "parseInt(c.substr(2,2),16),parseInt(c.substr(4,2),16)];}"
        "function mix(a,b,t){var x=hx(a),y=hx(b),o='#';"
          "for(var i=0;i<3;i++){var v=Math.round(x[i]+(y[i]-x[i])*t/255);"
            "o+=('0'+Math.max(0,Math.min(255,v)).toString(16)).slice(-2);}return o;}"
        "function derive(t){var L=(t.m==1);return {bg:t.bg,panel:t.p,panel2:t.p2,"
          "line:mix(t.p2,t.bg,L?135:150),text:L?'#1e242c':'#f3f6fa',"
          "dim:L?'#5e6876':'#b7c0cc',accent:t.a,ink:L?'#ffffff':'#080d14'};}"
        "function pal(k){"
          "if(P[k])return P[k];"
          "var t=byKey(k);return t?derive(t):P['0'];}"
        "function byKey(k){var s=parseInt(k,10)-CUSTOM_BASE;"
          "for(var i=0;i<CUSTOM.length;i++)if(CUSTOM[i].s===s)return CUSTOM[i];return null;}"
        "function isLightKey(k){var t=byKey(k);"
          "if(t)return t.m==1;return (parseInt(k,10)%2)==1;}"
        "function apply(){"
          "var k=input.value;var p=pal(k);"
          "var r=document.documentElement.style;"
          "r.setProperty('--bg',p.bg);r.setProperty('--panel',p.panel);r.setProperty('--panel-2',p.panel2);"
          "r.setProperty('--line',p.line);r.setProperty('--text',p.text);r.setProperty('--text-dim',p.dim);"
          "r.setProperty('--accent',p.accent);r.setProperty('--accent-ink',p.ink);"
          // The names /app.css reads (web_app_assets.h). A preview has no
          // derived input or line colours, so it borrows the nearest it has.
          "r.setProperty('--dim',p.dim);r.setProperty('--on-accent',p.ink);"
          "r.setProperty('--input',p.panel2);r.setProperty('--panel-3',p.panel2);"
          "r.setProperty('--line-hi',p.accent);"
          "r.colorScheme=isLightKey(k)?'light':'dark';"
        "}"
        "function select(k){input.value=k;"
          "var cards=document.querySelectorAll('#themeGrid .theme-card');"
          "for(var i=0;i<cards.length;i++)cards[i].classList.toggle('sel',cards[i].dataset.k==k);"
          "apply();}"
        "var grid=document.getElementById('themeGrid');"
        "function card(k,name,cols,cust){"
          "var c=document.createElement('div');"
          "c.className='theme-card'+(cust?' cust':'');c.dataset.k=k;"
          "var sw=document.createElement('div');sw.className='theme-sws';"
          "cols.forEach(function(col){"
            "var b=document.createElement('div');b.className='theme-sw';b.style.background=col;sw.appendChild(b);});"
          "var nm=document.createElement('div');nm.className='theme-name';nm.textContent=name;"
          "c.appendChild(sw);c.appendChild(nm);"
          "c.addEventListener('click',function(){select(k);});"
          "return c;}"
        // Case-insensitive substring on the name, matching the on-device filter.
        "var fbox=document.getElementById('themeFilter'),fcount=document.getElementById('themeCount');"
        "function pass(name){var f=(fbox.value||'').trim().toLowerCase();"
          "return !f||String(name).toLowerCase().indexOf(f)>=0;}"
        "function build(){"
          "grid.innerHTML='';"
          "var shown=0,total=0;"
          "Object.keys(P).forEach(function(k){"
            "var p=P[k],nm=NAMES[k]||('Theme '+k);total++;"
            "if(!pass(nm))return;shown++;"
            "grid.appendChild(card(k,nm,[p.bg,p.panel2,p.accent],false));});"
          "CUSTOM.forEach(function(t){"
            "var k=String(CUSTOM_BASE+t.s);total++;"
            "if(!pass(t.n))return;shown++;"
            "var c=card(k,t.n,[t.bg,t.p2,t.a],true);"
            "var ed=document.createElement('button');ed.type='button';"
            "ed.className='theme-btn-cnr theme-edit';ed.textContent='\\u270E';"
            "ed.title='Edit theme';"
            "ed.addEventListener('click',function(e){e.stopPropagation();openBuilder(t);});"
            "var dl=document.createElement('button');dl.type='button';"
            "dl.className='theme-btn-cnr theme-del';dl.textContent='-';"
            "dl.title='Delete theme';"
            "dl.addEventListener('click',function(e){e.stopPropagation();del(t);});"
            "c.appendChild(ed);c.appendChild(dl);"
            "grid.appendChild(c);});"
          // +New is an action, not a theme, so it goes away while filtering
          // rather than sitting at the end of every result set.
          "if(CUSTOM.length<CUSTOM_SLOTS&&!(fbox.value||'').trim()){"
            "var n=document.createElement('div');n.className='theme-card new';"
            "n.textContent='+New';n.title='Build a custom theme';"
            "n.addEventListener('click',function(){openBuilder();});"
            "grid.appendChild(n);}"
          "fcount.textContent=(shown<total)?(shown+' of '+total):'';"
          "var cards=document.querySelectorAll('#themeGrid .theme-card');"
          "for(var i=0;i<cards.length;i++)cards[i].classList.toggle('sel',cards[i].dataset.k==input.value);"
        "}"
        "fbox.addEventListener('input',build);"
        // Enter in the filter would submit the config form; with one match left
        // it should pick that theme instead, which is what the on-device filter
        // does too.
        "fbox.addEventListener('keydown',function(e){if(e.key!=='Enter')return;"
          "e.preventDefault();"
          "var only=document.querySelectorAll('#themeGrid .theme-card');"
          "if(only.length===1&&only[0].dataset.k!==undefined)select(only[0].dataset.k);});"
        // ── Builder ──
        "var back=document.getElementById('tbBack'),msg=document.getElementById('tbMsg');"
        "var F={name:'tbName',bg:'tbBg',pn:'tbPn',p2:'tbP2',ac:'tbAc',code:'tbCode'};"
        "function g(id){return document.getElementById(id);}"
        "function pairs(){return [['tbBg','tbBgP'],['tbPn','tbPnP'],['tbP2','tbP2P'],['tbAc','tbAcP']];}"
        "function norm(v){v=(v||'').trim();if(v.charAt(0)!='#')v='#'+v;"
          "return /^#[0-9a-fA-F]{6}$/.test(v)?v.toLowerCase():null;}"
        "function mode(){var r=document.querySelector('input[name=tbMode]:checked');"
          "return r?parseInt(r.value,10):0;}"
        "function setMode(m){var r=document.querySelector('input[name=tbMode][value=\"'+m+'\"]');"
          "if(r)r.checked=true;}"
        "function preview(){"
          "var b=norm(g('tbBg').value),p=norm(g('tbPn').value),"
            "q=norm(g('tbP2').value),a=norm(g('tbAc').value);"
          "if(!b||!p||!q||!a)return;"
          "var d=derive({m:mode(),bg:b,p:p,p2:q,a:a});"
          "var box=g('tbPrev');box.style.background=d.panel;box.style.borderColor=d.line;"
          "g('tbPrevT').style.color=d.text;g('tbPrevD').style.color=d.dim;"
          "var ac=g('tbPrevA');ac.style.background=d.accent;ac.style.color=d.ink;}"
        "pairs().forEach(function(pr){"
          "var t=g(pr[0]),c=g(pr[1]);"
          "t.addEventListener('input',function(){var v=norm(t.value);if(v){c.value=v;preview();}});"
          "c.addEventListener('input',function(){t.value=c.value;preview();});});"
        "Array.prototype.forEach.call(document.querySelectorAll('input[name=tbMode]'),"
          "function(r){r.addEventListener('change',preview);});"
        "function fill(t){g('tbName').value=t.n;setMode(t.m);"
          "g('tbBg').value=t.bg;g('tbPn').value=t.p;g('tbP2').value=t.p2;g('tbAc').value=t.a;"
          "pairs().forEach(function(pr){var v=norm(g(pr[0]).value);if(v)g(pr[1]).value=v;});"
          "preview();}"
        // -1 = building a new theme, otherwise the slot being edited. Save posts
        // it back so an edit overwrites in place instead of consuming a slot.
        "var editSlot=-1;"
        "function openBuilder(t){msg.textContent='';g('tbCode').value='';"
          "editSlot=t?t.s:-1;"
          "g('tbTitle').textContent=t?('Edit '+t.n):'New Theme';"
          "fill(t||{n:'',m:0,bg:'#10141d',p:'#1a2230',p2:'#232d3e',a:'#d7869d'});"
          "back.classList.add('on');g('tbName').focus();}"
        "function closeBuilder(){back.classList.remove('on');}"
        "g('tbCancel').addEventListener('click',closeBuilder);"
        "back.addEventListener('click',function(e){if(e.target===back)closeBuilder();});"
        "function post(url,body,ok){"
          "var x=new XMLHttpRequest();x.open('POST',url,true);"
          "x.setRequestHeader('Content-Type','application/x-www-form-urlencoded');"
          "x.onreadystatechange=function(){if(x.readyState!=4)return;"
            "var j=null;try{j=JSON.parse(x.responseText);}catch(_){}"
            "if(x.status>=200&&x.status<300&&j&&j.ok)ok(j);"
            "else msg.textContent=(j&&j.error)?j.error:'Request failed.';};"
          "x.send(body);}"
        "function stored(j){"
          // Replace in place when the slot already had a theme, so an import
          // over an existing slot does not produce two cards for it.
          "var found=false;"
          "for(var i=0;i<CUSTOM.length;i++)if(CUSTOM[i].s===j.slot){CUSTOM[i]=j.theme;found=true;}"
          "if(!found)CUSTOM.push(j.theme);"
          "CUSTOM.sort(function(a,b){return a.s-b.s;});"
          "build();select(String(CUSTOM_BASE+j.slot));closeBuilder();}"
        "g('tbSave').addEventListener('click',function(){"
          "var nm=(g('tbName').value||'').trim();"
          "if(!nm){msg.textContent='Give the theme a name.';return;}"
          "var b=norm(g('tbBg').value),p=norm(g('tbPn').value),"
            "q=norm(g('tbP2').value),a=norm(g('tbAc').value);"
          "if(!b||!p||!q||!a){msg.textContent='Colors must be hex like #1a2230.';return;}"
          "msg.textContent='Saving...';"
          "post('/theme-save','name='+encodeURIComponent(nm)+'&mode='+mode()+"
            "'&bg='+encodeURIComponent(b)+'&panel='+encodeURIComponent(p)+"
            "'&panel2='+encodeURIComponent(q)+'&accent='+encodeURIComponent(a)+"
            "(editSlot>=0?('&slot='+editSlot):''),stored);});"
        "g('tbImport').addEventListener('click',function(){"
          "var c=(g('tbCode').value||'').trim();"
          "if(!c){msg.textContent='Paste a share code first.';return;}"
          "msg.textContent='Reading code...';"
          // Decoded on the device so the browser never reimplements the format.
          // preview=1 fills the form instead of storing, which lets someone
          // rename or adjust an imported theme before committing a slot.
          "post('/theme-save','preview=1&code='+encodeURIComponent(c),function(j){"
            "msg.textContent='Code loaded — Save Theme to keep it.';fill(j.theme);});});"
        "function del(t){"
          "if(!window.confirm('Delete the theme \"'+t.n+'\"? This cannot be undone.'))return;"
          "post('/theme-delete','slot='+t.s,function(j){"
            "CUSTOM=CUSTOM.filter(function(x){return x.s!==t.s;});"
            "if(parseInt(input.value,10)===CUSTOM_BASE+t.s)input.value='0';"
            "build();select(input.value);});}"
        "build();"
        "select(input.value);"
        "})();"
        "</script>";

// ── Timezone table ────────────────────────────────────────────

struct TzOption { const char *label; const char *posix; };
static const TzOption kTzOptions[] = {
    { "UTC",                                "UTC0"                                   },
    { "US — Hawaii (UTC-10)",               "HST10"                                  },
    { "US — Alaska (UTC-9/-8)",             "AKST9AKDT,M3.2.0,M11.1.0"              },
    { "US — Pacific (UTC-8/-7)",            "PST8PDT,M3.2.0,M11.1.0"                },
    { "US — Mountain (UTC-7/-6)",           "MST7MDT,M3.2.0,M11.1.0"                },
    { "US — Arizona, no DST (UTC-7)",       "MST7"                                   },
    { "US — Central (UTC-6/-5)",            "CST6CDT,M3.2.0,M11.1.0"                },
    { "US — Eastern (Detroit) (UTC-5/-4)",  "EST5EDT,M3.2.0,M11.1.0"                },
    { "Canada — Atlantic (UTC-4/-3)",       "AST4ADT,M3.2.0/0,M11.1.0/0"            },
    { "Brazil — Brasilia (UTC-3)",          "BRT3BRST,M10.3.0/0,M2.3.0/0"           },
    { "Argentina (UTC-3)",                  "ART3"                                   },
    { "UK (UTC+0/+1)",                      "GMT0BST,M3.5.0/1,M10.5.0"              },
    { "Western Europe — CET (UTC+1/+2)",    "CET-1CEST,M3.5.0,M10.5.0/3"           },
    { "Eastern Europe — EET (UTC+2/+3)",    "EET-2EEST,M3.5.0/3,M10.5.0/4"         },
    { "Russia — Moscow (UTC+3)",            "MSK-3"                                  },
    { "India (UTC+5:30)",                   "IST-5:30"                               },
    { "China / Singapore (UTC+8)",          "CST-8"                                  },
    { "Japan / Korea (UTC+9)",              "JST-9"                                  },
    { "Australia — Perth (UTC+8)",          "AWST-8"                                 },
    { "Australia — Eastern (UTC+10/+11)",   "AEST-10AEDT,M10.1.0,M4.1.0/3"         },
    { "New Zealand (UTC+12/+13)",           "NZST-12NZDT,M9.5.0,M4.1.0/3"          },
};
static const int kTzCount = (int)(sizeof(kTzOptions) / sizeof(kTzOptions[0]));

// ── Shared page output helpers ────────────────────────────────

// Helper: flush a chunk of HTML and reset the buffer.
//
// Writes are skipped once the client is gone — a browser that navigated away or
// refreshed mid-render would otherwise leave us writing into a dead socket,
// blocking the main loop for the client timeout on every remaining chunk. The
// page keeps building (cheap, no network) and simply lands nowhere.
// Set when a response is abandoned mid-render; cleared at the start of each page.
static bool gSendAborted = false;

// Largest single write handed to the socket. Comfortably inside one TCP segment
// so a write can't outrun the space the writability check just observed.
static const size_t kFlashChunkBytes = 512;

// Waits (briefly) for the socket to accept data, and reports whether it will.
//
// This exists because WiFiClient::write() cannot be bounded from outside: it
// sends with MSG_DONTWAIT behind a select() with a hardcoded 1 s timeout and 10
// retries — a partial send re-arms them — and it ignores setTimeout(). When the
// response outgrows LWIP's TCP send buffer (TCP_SND_BUF, 5744 bytes by default)
// and ACKs stop arriving, a single sendContent() blocks the main loop for a full
// 10 s, taking the UI and radio down with it. Checking writability ourselves
// first means we never enter that loop unless the socket is ready.
// How long a single write may wait for the socket to accept anything before the
// response is written off. Was 250 ms, which the logs showed abandoning pages
// over chunks that merely took 307 ms — a slow link should give a slow page,
// not no page. A dead client is still caught immediately by connected(), and a
// reset arrives as an error from write(), so this only bounds genuine
// slowness.
static const uint32_t kWriteWindowMs = 1000;

