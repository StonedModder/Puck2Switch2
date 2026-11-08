// web_ui.h — the editor UI, embedded in flash and served from /
// Kept in one file so it can be served with no filesystem and no build step.
#pragma once

static const char *WEB_UI_HTML = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Puck2Switch2 Macros</title>
<style>
:root{--bg:#0f1115;--panel:#171a21;--panel2:#1e222b;--line:#2a2f3a;--tx:#e8ecf3;--dim:#8b93a5;--acc:#4f9cf9;--ok:#3ddc84;--warn:#ffb020;--bad:#ff5c5c}
*{box-sizing:border-box}
body{margin:0;font:14px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--tx)}
header{display:flex;gap:10px;align-items:center;padding:10px 14px;border-bottom:1px solid var(--line);background:var(--panel);position:sticky;top:0;z-index:5;flex-wrap:wrap}
h1{font-size:15px;margin:0 8px 0 0;font-weight:650;letter-spacing:.3px}
.chip{font-size:11.5px;padding:3px 8px;border-radius:999px;background:var(--panel2);border:1px solid var(--line);color:var(--dim);white-space:nowrap}
.chip.on{color:#0c1117;background:var(--ok);border-color:var(--ok);font-weight:600}
.chip.rec{color:#0c1117;background:var(--bad);border-color:var(--bad);font-weight:600}
.chip.play{color:#0c1117;background:var(--acc);border-color:var(--acc);font-weight:600}
.spacer{flex:1}
button{font:inherit;color:var(--tx);background:var(--panel2);border:1px solid var(--line);border-radius:8px;padding:7px 12px;cursor:pointer}
button:hover{border-color:#3b4250}
button:active{transform:translateY(1px)}
button.pri{background:var(--acc);border-color:var(--acc);color:#08111d;font-weight:650}
button.dan{color:var(--bad)}
button:disabled{opacity:.45;cursor:not-allowed}
main{display:grid;grid-template-columns:250px 1fr;gap:14px;padding:14px;align-items:start}
@media(max-width:820px){main{grid-template-columns:1fr}}
.card{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:12px}
.card h2{font-size:12px;text-transform:uppercase;letter-spacing:.7px;color:var(--dim);margin:0 0 10px}
.lib{display:flex;flex-direction:column;gap:6px;max-height:52vh;overflow:auto}
.lib button{display:flex;justify-content:space-between;gap:8px;text-align:left;padding:8px 10px;font-size:13px}
.lib .m{color:var(--dim);font-size:11px;white-space:nowrap}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.row>input{flex:1;min-width:120px}
input,select,textarea{font:inherit;color:var(--tx);background:#12151b;border:1px solid var(--line);border-radius:8px;padding:7px 9px}
textarea{width:100%;min-height:90px;font-family:ui-monospace,Menlo,monospace;font-size:12px}
label.f{display:block;font-size:11px;color:var(--dim);margin:0 0 4px}
.steps{margin-top:12px;display:flex;flex-direction:column;gap:6px;max-height:44vh;overflow:auto}
.step{display:grid;grid-template-columns:34px 78px 1fr auto;gap:8px;align-items:center;padding:8px;border:1px solid var(--line);border-radius:10px;background:#12151b;cursor:pointer}
.step.sel{border-color:var(--acc);box-shadow:0 0 0 1px var(--acc) inset}
.step .i{color:var(--dim);font-size:12px;text-align:right}
.step input{width:72px;padding:5px 6px}
.step .b{display:flex;gap:4px;flex-wrap:wrap}
.tag{font-size:11px;padding:2px 7px;border-radius:6px;background:#232a36;color:var(--tx);border:1px solid var(--line)}
.tag.axis{background:#1f2a3a;color:#9cc4ff}
.pad{display:grid;grid-template-columns:repeat(6,1fr);gap:6px;margin-top:8px}
.pad button{padding:10px 4px;font-size:12px;text-align:center}
.pad button.on{background:var(--acc);border-color:var(--acc);color:#08111d;font-weight:650}
.axes{display:grid;grid-template-columns:repeat(2,1fr);gap:10px;margin-top:10px}
.axis{background:#12151b;border:1px solid var(--line);border-radius:10px;padding:8px}
.axis .t{font-size:11px;color:var(--dim);display:flex;justify-content:space-between}
input[type=range]{width:100%;accent-color:var(--acc)}
.hint{font-size:11.5px;color:var(--dim);margin-top:8px}
.bar{height:4px;background:#232a36;border-radius:4px;overflow:hidden;margin-top:8px}
.bar>i{display:block;height:100%;width:0;background:var(--acc);transition:width .08s linear}
dialog{background:var(--panel);color:var(--tx);border:1px solid var(--line);border-radius:14px;padding:16px;max-width:640px;width:92vw}
dialog::backdrop{background:#000a}
.toast{position:fixed;right:14px;bottom:14px;background:var(--panel2);border:1px solid var(--line);border-radius:10px;padding:10px 14px;opacity:0;transition:opacity .2s;pointer-events:none;max-width:70vw}
.toast.show{opacity:1}
.kbd{font:11px ui-monospace,monospace;background:#12151b;border:1px solid var(--line);border-radius:5px;padding:1px 5px}
</style>
</head>
<body>
<header>
  <h1>Puck2Switch2</h1>
  <span class="chip" id="c-pad">controller</span>
  <span class="chip" id="c-con">console</span>
  <span class="chip" id="c-rec">REC</span>
  <span class="chip" id="c-play">PLAY</span>
  <span class="spacer"></span>
  <button id="b-rec">● Record</button>
  <button id="b-play" class="pri">▶ Play</button>
  <button id="b-stop">■ Stop</button>
  <button id="b-save" class="pri">Save</button>
</header>

<main>
  <section class="card">
    <h2>Library</h2>
    <div class="row" style="margin-bottom:8px">
      <button id="b-new" style="flex:1">+ New</button>
      <button id="b-import">Import</button>
    </div>
    <div class="lib" id="lib"></div>
    <p class="hint">Saved on the adapter. Draft autosaves as you type.</p>

    <h2 style="margin-top:16px">Test rumble</h2>
    <div class="row">
      <button id="b-rumble" style="flex:1">Vibrate controller</button>
    </div>
    <p class="hint" id="rumble-out">Buzzes the Steam Controller for 500 ms.</p>
  </section>

  <section class="card">
    <h2>Editor</h2>
    <div class="row">
      <div style="flex:1;min-width:160px"><label class="f">Name</label><input id="f-name" placeholder="Untitled macro"></div>
      <div style="flex:1;min-width:140px"><label class="f">Game</label><input id="f-game" placeholder="optional"></div>
      <div style="flex:1;min-width:120px"><label class="f">Author</label><input id="f-author" placeholder="optional"></div>
    </div>

    <div class="row" style="margin-top:12px">
      <button id="b-add">+ Add step</button>
      <button id="b-dup">Duplicate</button>
      <button id="b-up">↑</button>
      <button id="b-down">↓</button>
      <button id="b-del" class="dan">Delete step</button>
      <span class="spacer"></span>
      <span class="chip" id="c-dur">0 ms</span>
    </div>

    <div class="steps" id="steps"></div>

    <h2 style="margin-top:16px">Inputs for selected step</h2>
    <div class="pad" id="pad"></div>
    <div class="axes">
      <div class="axis"><div class="t"><span>Left X</span><span id="v-lx">2048</span></div>
        <input type="range" id="s-lx" min="0" max="4095" value="2048"></div>
      <div class="axis"><div class="t"><span>Left Y</span><span id="v-ly">2048</span></div>
        <input type="range" id="s-ly" min="0" max="4095" value="2048"></div>
      <div class="axis"><div class="t"><span>Right X</span><span id="v-rx">2048</span></div>
        <input type="range" id="s-rx" min="0" max="4095" value="2048"></div>
      <div class="axis"><div class="t"><span>Right Y</span><span id="v-ry">2048</span></div>
        <input type="range" id="s-ry" min="0" max="4095" value="2048"></div>
    </div>
    <p class="hint">Click a button to toggle it in the selected step. <span class="kbd">space</span> play · <span class="kbd">R</span> record · <span class="kbd">S</span> save</p>
    <div class="bar"><i id="prog"></i></div>
  </section>
</main>

<dialog id="dlg-import">
  <h2 style="margin-top:0">Import macro</h2>
  <p class="hint">Paste a macro JSON or a share code, or choose a file.</p>
  <textarea id="imp-text" placeholder='{"v":1,"steps":[{"d":80,"b":["A"]}]}'></textarea>
  <div class="row" style="margin-top:10px">
    <input type="file" id="imp-file" accept=".json,application/json">
    <span class="spacer"></span>
    <button id="imp-cancel">Cancel</button>
    <button id="imp-ok" class="pri">Import</button>
  </div>
</dialog>

<div class="toast" id="toast"></div>

<script>
"use strict";
const BTNS = ["A","B","X","Y","L","R","ZL","ZR","Plus","Minus","Home","Capture","LStick","RStick","Up","Down","Left","Right","GL","GR"];
const C=2048;
const el = id => document.getElementById(id);
let macro = { name:"Untitled macro", game:"", author:"", steps:[ {d:100,b:[],lx:C,ly:C,rx:C,ry:C} ] };
let sel = 0, dirty = false, saveTimer = null, cookieIdx = -1;
let playing = false, recording = false;

/* ---------------- storage: chunked cookies + localStorage mirror ---------- */
const DRAFT_KEY = "p2s2.draft";
function setCookie(n,v,days){ document.cookie = n+"="+v+";path=/;max-age="+(days*86400)+";SameSite=Lax"; }
function getCookie(n){
  const m = document.cookie.match(new RegExp("(?:^|; )"+n.replace(/\./g,"\\.")+"=([^;]*)"));
  return m ? m[1] : null;
}
function delCookie(n){ setCookie(n,"",0); }
/* Cookies cap at ~4KB each, so the draft is chunked. This is the primary store
   because it survives reloads even when storage is blocked (private mode). */
function saveDraft(){
  const payload = JSON.stringify({ savedAt: Date.now(), macro, sel });
  const enc = encodeURIComponent(payload);
  const CH = 3200;
  const n = Math.ceil(enc.length / CH);
  if (n > 12) {                 // ~38KB: past what cookies should carry
    try { localStorage.setItem(DRAFT_KEY, payload); } catch(e){}
    return;
  }
  for (let i=0;i<n;i++) setCookie("p2s2_d"+i, enc.slice(i*CH,(i+1)*CH), 365);
  for (let i=n;i<=cookieIdx;i++) delCookie("p2s2_d"+i);
  cookieIdx = n-1;
  setCookie("p2s2_dn", String(n), 365);
  try { localStorage.setItem(DRAFT_KEY, payload); } catch(e){}   // mirror
}
function loadDraft(){
  let fromCookie = null;
  const n = parseInt(getCookie("p2s2_dn")||"0",10);
  if (n>0){
    let s="";
    for (let i=0;i<n;i++){ const c=getCookie("p2s2_d"+i); if (c==null){ s=null; break; } s+=c; }
    if (s) { try { fromCookie = JSON.parse(decodeURIComponent(s)); } catch(e){} }
  }
  let fromLS = null;
  try { const raw = localStorage.getItem(DRAFT_KEY); if (raw) fromLS = JSON.parse(raw); } catch(e){}
  const a = fromCookie && fromCookie.macro ? fromCookie : null;
  const b = fromLS && fromLS.macro ? fromLS : null;
  if (!a && !b) return false;
  const pick = (!a) ? b : (!b ? a : (a.savedAt >= b.savedAt ? a : b));
  macro = pick.macro; sel = Math.min(pick.sel||0, macro.steps.length-1);
  if (!Array.isArray(macro.steps) || !macro.steps.length) return false;
  return true;
}
function markDirty(){ dirty = true; clearTimeout(saveTimer); saveTimer = setTimeout(()=>{ saveDraft(); dirty=false; }, 350); }
function flush(){ if (dirty) { saveDraft(); dirty=false; } }
addEventListener("pagehide", flush);
addEventListener("visibilitychange", ()=>{ if (document.visibilityState==="hidden") flush(); });
addEventListener("beforeunload", flush);

/* ---------------- api ------------------------------------------------------ */
async function api(path, opts){
  const r = await fetch(path, Object.assign({headers:{"Content-Type":"application/json"}}, opts||{}));
  const text = await r.text();
  let data = null; try { data = text ? JSON.parse(text) : null; } catch(e){ data = {raw:text}; }
  if (!r.ok) throw new Error((data && data.error) || ("HTTP "+r.status));
  return data;
}
function toast(msg, ms){
  const t = el("toast"); t.textContent = msg; t.classList.add("show");
  clearTimeout(t._t); t._t = setTimeout(()=>t.classList.remove("show"), ms||2200);
}

/* ---------------- rendering ------------------------------------------------ */
function stepButtons(st){ return (st.b||[]).slice().sort((x,y)=>BTNS.indexOf(x)-BTNS.indexOf(y)); }
function totalMs(){ return macro.steps.reduce((a,s)=>a+(+s.d||0),0); }

function render(){
  el("f-name").value = macro.name || "";
  el("f-game").value = macro.game || "";
  el("f-author").value = macro.author || "";
  el("c-dur").textContent = totalMs() + " ms · " + macro.steps.length + " steps";

  const box = el("steps"); box.innerHTML = "";
  macro.steps.forEach((st,i)=>{
    const d = document.createElement("div");
    d.className = "step" + (i===sel ? " sel" : "");
    d.onclick = (e)=>{ if (e.target.tagName!=="INPUT") { sel=i; render(); } };

    const idx = document.createElement("div"); idx.className="i"; idx.textContent = i+1;

    const dur = document.createElement("input");
    dur.type="number"; dur.min="10"; dur.max="60000"; dur.step="10"; dur.value = st.d;
    dur.title="hold (ms)";
    dur.onchange = ()=>{ st.d = Math.max(10, Math.min(60000, +dur.value||10)); markDirty(); render(); };

    const b = document.createElement("div"); b.className="b";
    const names = stepButtons(st);
    if (!names.length) { const t=document.createElement("span"); t.className="tag"; t.style.opacity=".5"; t.textContent="neutral"; b.appendChild(t); }
    names.forEach(n=>{ const t=document.createElement("span"); t.className="tag"; t.textContent=n; b.appendChild(t); });
    const axes = [];
    if (+st.lx!==C) axes.push("LX "+st.lx);
    if (+st.ly!==C) axes.push("LY "+st.ly);
    if (+st.rx!==C) axes.push("RX "+st.rx);
    if (+st.ry!==C) axes.push("RY "+st.ry);
    if (axes.length){ const t=document.createElement("span"); t.className="tag axis"; t.textContent=axes.join(" "); b.appendChild(t); }

    d.append(idx, dur, b);
    box.appendChild(d);
  });

  const pad = el("pad"); pad.innerHTML = "";
  const cur = macro.steps[sel] || {b:[]};
  BTNS.forEach(n=>{
    const btn = document.createElement("button");
    btn.textContent = n;
    if ((cur.b||[]).includes(n)) btn.classList.add("on");
    btn.onclick = ()=>{ toggleButton(n); };
    pad.appendChild(btn);
  });

  const st = macro.steps[sel] || {};
  [["lx","s-lx","v-lx"],["ly","s-ly","v-ly"],["rx","s-rx","v-rx"],["ry","s-ry","v-ry"]].forEach(([k,sid,vid])=>{
    el(sid).value = st[k] ?? C; el(vid).textContent = st[k] ?? C;
  });
}

function toggleButton(n){
  const st = macro.steps[sel]; if (!st) return;
  st.b = st.b || [];
  const i = st.b.indexOf(n);
  if (i>=0) st.b.splice(i,1); else st.b.push(n);
  markDirty(); render();
}

/* ---------------- editing -------------------------------------------------- */
function addStep(after){
  const at = (after===undefined ? sel+1 : after);
  const prev = macro.steps[sel] || {lx:C,ly:C,rx:C,ry:C};
  macro.steps.splice(at, 0, {d:100,b:[],lx:prev.lx,ly:prev.ly,rx:prev.rx,ry:prev.ry});
  sel = at; markDirty(); render();
}
function delStep(){
  if (macro.steps.length<=1){ toast("A macro needs at least one step"); return; }
  macro.steps.splice(sel,1); sel = Math.max(0, sel-1); markDirty(); render();
}
function moveStep(dir){
  const j = sel+dir;
  if (j<0 || j>=macro.steps.length) return;
  const t = macro.steps[sel]; macro.steps[sel]=macro.steps[j]; macro.steps[j]=t;
  sel = j; markDirty(); render();
}

/* ---------------- library -------------------------------------------------- */
let library = [];
async function refreshLib(){
  try { library = await api("/api/macros"); } catch(e){ library = []; }
  const box = el("lib"); box.innerHTML = "";
  if (!library.length){ const p=document.createElement("p"); p.className="hint"; p.textContent="No saved macros yet."; box.appendChild(p); return; }
  library.forEach(m=>{
    const b = document.createElement("button");
    const l = document.createElement("span"); l.textContent = m.name || m.id;
    const r = document.createElement("span"); r.className="m"; r.textContent = m.steps+"·"+m.ms+"ms";
    b.append(l,r);
    b.onclick = ()=>loadMacro(m.id);
    box.appendChild(b);
  });
}
async function loadMacro(id){
  try {
    const data = await api("/api/macro?id="+encodeURIComponent(id));
    const steps = (data.steps||[]).map(s=>({d:+s.d||10,b:(s.b||[]).filter(x=>typeof x==="string"),
                                            lx:s.lx??C,ly:s.ly??C,rx:s.rx??C,ry:s.ry??C}));
    if (!steps.length) throw new Error("macro has no steps");
    macro = { name:data.name||id, game:data.game||"", author:data.author||"", steps };
    macro.id = data.id || id;
    sel = 0; markDirty(); render(); toast("Loaded "+(data.name||id));
  } catch(e){ toast("Load failed: "+e.message); }
}
async function saveMacro(){
  flush();
  try {
    const body = JSON.stringify({ v:1, id:macro.id||"", name:macro.name||"Untitled macro",
      game:macro.game||"", author:macro.author||"",
      created: macro.created || Math.floor(Date.now()/1000),
      steps: macro.steps.map(s=>({d:+s.d, b:stepButtons(s),
        lx:+s.lx, ly:+s.ly, rx:+s.rx, ry:+s.ry})) });
    const q = macro.id ? ("?id="+encodeURIComponent(macro.id)) : "";
    const res = await api("/api/macro"+q, {method:"POST", body});
    macro.id = res.id; toast("Saved as "+res.id); refreshLib();
  } catch(e){ toast("Save failed: "+e.message, 3200); }
}

/* ---------------- play / record -------------------------------------------- */
function currentJson(){
  return JSON.stringify({ v:1, name:macro.name||"Untitled macro", game:macro.game||"",
    author:macro.author||"", steps: macro.steps.map(s=>({d:+s.d, b:stepButtons(s),
      lx:+s.lx, ly:+s.ly, rx:+s.rx, ry:+s.ry})) });
}
async function play(){
  try { const r = await api("/api/play", {method:"POST", body: currentJson()});
        playing = true; toast("Playing "+r.ms+" ms"); }
  catch(e){ toast("Play failed: "+e.message, 3200); }
}
async function stopAll(){
  try { await api("/api/stop", {method:"POST", body:"{}"}); } catch(e){}
  if (recording){ recording = false; }
  playing = false;
}
async function toggleRecord(){
  try {
    if (!recording){
      const r = await api("/api/record", {method:"POST", body:JSON.stringify({action:"start"})});
      recording = true; toast("Recording — play the sequence on your controller");
    } else {
      const data = await api("/api/record", {method:"POST", body:JSON.stringify({action:"stop"})});
      recording = false;
      const steps = (data.steps||[]).map(s=>({d:+s.d||10, b:(s.b||[]).filter(x=>typeof x==="string"),
                                              lx:s.lx??C,ly:s.ly??C,rx:s.rx??C,ry:s.ry??C}));
      if (steps.length){ macro.steps = steps; sel = 0; markDirty(); render(); toast("Captured "+steps.length+" steps"); }
      else toast("Nothing was recorded");
    }
  } catch(e){ toast("Record failed: "+e.message, 3200); }
}

/* ---------------- import / export ------------------------------------------ */
function download(){
  const blob = new Blob([currentJson()], {type:"application/json"});
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = (macro.id || (macro.name||"macro").toLowerCase().replace(/[^a-z0-9]+/g,"-")) + ".json";
  a.click(); setTimeout(()=>URL.revokeObjectURL(a.href), 4000);
}
function shareCode(){ return btoa(unescape(encodeURIComponent(currentJson()))); }
function applyImported(text){
  const raw = (text||"").trim();
  let doc;
  try { doc = JSON.parse(raw); }
  catch(e){
    try { doc = JSON.parse(decodeURIComponent(escape(atob(raw)))); }
    catch(e2){ throw new Error("not JSON or a share code"); }
  }
  const steps = (doc.steps||[]).map(s=>({d:+s.d||10, b:(s.b||[]).filter(x=>typeof x==="string"),
                                         lx:s.lx??C,ly:s.ly??C,rx:s.rx??C,ry:s.ry??C}));
  if (!steps.length) throw new Error("macro has no steps");
  macro = { name:doc.name||"Imported macro", game:doc.game||"", author:doc.author||"",
            id:doc.id||"", steps };
  sel = 0; markDirty(); render(); toast("Imported "+steps.length+" steps");
}

/* ---------------- status --------------------------------------------------- */
async function poll(){
  try {
    const s = await api("/api/status");
    el("c-pad").textContent = s.sc2 ? "controller connected" : "no controller";
    el("c-pad").className = "chip" + (s.sc2?" on":"");
    el("c-con").textContent = s.console ? (s.streaming ? "console streaming" : "console connected") : "no console";
    el("c-con").className = "chip" + (s.console?" on":"");
    el("c-rec").className = "chip" + (s.recording?" rec":"");
    el("c-rec").textContent = s.recording ? ("REC "+Math.round(s.rec_ms/1000)+"s") : "REC";
    el("c-play").className = "chip" + (s.playing?" play":"");
    el("c-play").textContent = s.playing ? "PLAY" : "PLAY";
    recording = !!s.recording; playing = !!s.playing;
    const pct = s.dur ? Math.min(100, Math.round(100*s.pos/s.dur)) : 0;
    el("prog").style.width = (s.playing ? pct : 0) + "%";
    el("b-rec").textContent = recording ? "■ Stop rec" : "● Record";
  } catch(e){ /* device busy or rebooting */ }
}

/* ---------------- wiring --------------------------------------------------- */
el("b-new").onclick = ()=>{
  macro = { name:"Untitled macro", game:"", author:"", steps:[{d:100,b:[],lx:C,ly:C,rx:C,ry:C}] };
  sel = 0; markDirty(); render();
};
el("b-add").onclick = ()=>addStep();
el("b-dup").onclick = ()=>{ const s = macro.steps[sel]; if (s) { const c=JSON.parse(JSON.stringify(s)); macro.steps.splice(sel+1,0,c); sel++; markDirty(); render(); } };
el("b-del").onclick = delStep;
el("b-up").onclick = ()=>moveStep(-1);
el("b-down").onclick = ()=>moveStep(1);
el("b-play").onclick = play;
el("b-stop").onclick = stopAll;
el("b-rec").onclick = toggleRecord;
el("b-save").onclick = saveMacro;
el("b-rumble").onclick = async ()=>{
  const out = el("rumble-out");
  out.textContent = "Vibrating...";
  try {
    const r = await api("/api/rumble?l=60000&r=60000&ms=500");
    out.textContent = "Sent: " + JSON.stringify(r);
  } catch(e){ out.textContent = "Failed: " + e.message; }
};
el("b-import").onclick = ()=>{ el("imp-text").value=""; el("dlg-import").showModal(); };
el("imp-cancel").onclick = ()=>el("dlg-import").close();
el("imp-ok").onclick = ()=>{ try { applyImported(el("imp-text").value); el("dlg-import").close(); } catch(e){ toast(e.message,3200); } };
el("imp-file").onchange = (ev)=>{
  const f = ev.target.files[0]; if (!f) return;
  const r = new FileReader();
  r.onload = ()=>{ try { applyImported(String(r.result)); el("dlg-import").close(); } catch(e){ toast(e.message,3200); } };
  r.readAsText(f);
};
["name","game","author"].forEach(k=>{
  el("f-"+k).oninput = ()=>{ macro[k] = el("f-"+k).value; markDirty(); };
});
[["lx","s-lx","v-lx"],["ly","s-ly","v-ly"],["rx","s-rx","v-rx"],["ry","s-ry","v-ry"]].forEach(([k,sid,vid])=>{
  el(sid).oninput = ()=>{
    const v = +el(sid).value;
    const st = macro.steps[sel]; if (st){ st[k]=v; el(vid).textContent=v; markDirty(); }
  };
  el(sid).onchange = ()=>render();
});
addEventListener("keydown", (e)=>{
  if (e.target.matches("input,textarea")) return;
  if (e.code==="Space"){ e.preventDefault(); play(); }
  else if (e.key==="r"||e.key==="R"){ e.preventDefault(); toggleRecord(); }
  else if (e.key==="s"||e.key==="S"){ e.preventDefault(); saveMacro(); }
});
window.addEventListener("beforeunload", (e)=>{
  flush();
  if (dirty) { e.preventDefault(); e.returnValue=""; }
});

/* ---------------- boot ----------------------------------------------------- */
(function boot(){
  const restored = loadDraft();
  render();
  refreshLib();
  poll(); setInterval(poll, 1000);
  if (restored) toast("Restored your draft");
})();
</script>
</body>
</html>
)HTML";
