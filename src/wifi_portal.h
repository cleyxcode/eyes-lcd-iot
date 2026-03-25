#pragma once

// HTML Captive Portal untuk konfigurasi WiFi Siram Pintar
// Disimpan di flash (PROGMEM) agar tidak memakan RAM
const char WIFI_PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">
<title>Siram Pintar - Setup WiFi</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#f0faf4;min-height:100vh}

  /* Header */
  .header{background:linear-gradient(135deg,#28AF60,#1e8a49);padding:20px 24px;color:white}
  .header-inner{display:flex;align-items:center;gap:12px}
  .logo{width:44px;height:44px;background:rgba(255,255,255,0.2);border-radius:12px;display:flex;align-items:center;justify-content:center;font-size:22px}
  .header h1{font-size:18px;font-weight:700}
  .header p{font-size:12px;opacity:0.8;margin-top:2px}

  /* Container */
  .container{max-width:480px;margin:0 auto;padding:20px 16px}

  /* Card */
  .card{background:white;border-radius:16px;padding:20px;margin-bottom:14px;box-shadow:0 2px 8px rgba(0,0,0,0.07)}

  /* Tabs */
  .tabs{display:flex;background:#f0faf4;border-radius:10px;padding:3px;margin-bottom:18px}
  .tab{flex:1;padding:9px;text-align:center;border-radius:8px;cursor:pointer;font-size:13px;color:#666;border:none;background:transparent;font-weight:500;transition:all .2s}
  .tab.active{background:white;color:#28AF60;font-weight:700;box-shadow:0 1px 4px rgba(0,0,0,0.1)}

  /* Scan button */
  .scan-btn{display:flex;align-items:center;justify-content:center;gap:8px;width:100%;padding:11px;border-radius:10px;border:1.5px solid #28AF60;background:transparent;color:#28AF60;font-size:13px;font-weight:600;cursor:pointer;margin-bottom:14px;transition:background .2s}
  .scan-btn:hover{background:#f0faf4}
  .scan-btn:disabled{opacity:.5;cursor:not-allowed}

  /* Network list */
  .net-list{display:flex;flex-direction:column;gap:8px;max-height:280px;overflow-y:auto}
  .net-item{display:flex;align-items:center;padding:12px 14px;border-radius:12px;border:2px solid #ececec;cursor:pointer;transition:all .15s;user-select:none}
  .net-item:hover{border-color:#a8dfc0;background:#f7fdf9}
  .net-item.selected{border-color:#28AF60;background:#f0faf4}
  .net-name{flex:1;font-size:14px;font-weight:500;color:#1a1a1a;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
  .net-meta{display:flex;align-items:center;gap:6px;flex-shrink:0}
  .signal{display:flex;gap:2px;align-items:flex-end}
  .bar{width:4px;border-radius:2px;background:#e0e0e0}
  .bar.on{background:#28AF60}
  .lock{font-size:12px;color:#aaa}

  /* Password section */
  .pass-section{margin-top:16px;padding-top:16px;border-top:1px solid #f0f0f0}
  .selected-badge{display:flex;align-items:center;gap:8px;background:#f0faf4;border-radius:10px;padding:10px 14px;margin-bottom:14px;font-size:13px;color:#28AF60;font-weight:600}

  /* Input */
  .field{margin-bottom:14px}
  .field label{display:block;font-size:12px;font-weight:600;color:#777;text-transform:uppercase;letter-spacing:.5px;margin-bottom:6px}
  .field input{width:100%;padding:12px 14px;border:1.5px solid #e0e0e0;border-radius:12px;font-size:14px;outline:none;transition:border .2s;color:#1a1a1a}
  .field input:focus{border-color:#28AF60}
  .field input:disabled{background:#f8f8f8;color:#aaa}
  .pass-wrap{position:relative}
  .pass-wrap input{padding-right:46px}
  .eye-btn{position:absolute;right:12px;top:50%;transform:translateY(-50%);background:none;border:none;cursor:pointer;font-size:17px;padding:4px}

  /* Buttons */
  .btn{width:100%;padding:14px;border-radius:14px;border:none;font-size:15px;font-weight:700;cursor:pointer;transition:all .2s;display:flex;align-items:center;justify-content:center;gap:8px}
  .btn-primary{background:#28AF60;color:white}
  .btn-primary:hover{background:#229952}
  .btn-primary:disabled{background:#a0d4b8;cursor:not-allowed}
  .btn-outline{background:transparent;color:#28AF60;border:2px solid #28AF60;margin-top:10px}
  .btn-outline:hover{background:#f0faf4}

  /* Loading spinner */
  .spinner{width:20px;height:20px;border:2.5px solid rgba(255,255,255,.3);border-top-color:white;border-radius:50%;animation:spin .7s linear infinite;flex-shrink:0}
  .spinner.green{border:2.5px solid #e0e0e0;border-top-color:#28AF60}
  @keyframes spin{to{transform:rotate(360deg)}}

  /* Alert */
  .alert{padding:11px 14px;border-radius:10px;font-size:13px;margin-top:12px;display:flex;align-items:flex-start;gap:8px;line-height:1.4}
  .alert-error{background:#fff5f5;color:#c0392b;border:1px solid #fcc}
  .alert-info{background:#e8f4fe;color:#1a5a8c;border:1px solid #b8d8f4}
  .hidden{display:none!important}

  /* Panel: Connecting */
  .connect-panel{text-align:center;padding:28px 20px}
  .connect-panel .big-spinner{width:52px;height:52px;border:4px solid #e0e0e0;border-top-color:#28AF60;border-radius:50%;animation:spin .8s linear infinite;margin:0 auto 16px}
  .connect-panel h3{color:#1a1a1a;font-size:16px;margin-bottom:6px}
  .connect-panel p{color:#999;font-size:13px;line-height:1.5}

  /* Panel: Success */
  .result-panel{text-align:center;padding:28px 16px}
  .result-icon{font-size:56px;margin-bottom:14px}
  .result-panel h2{font-size:20px;margin-bottom:8px}
  .result-panel.success h2{color:#28AF60}
  .result-panel.error h2{color:#c0392b}
  .result-panel p{color:#666;font-size:14px;line-height:1.5}
  .ip-chip{display:inline-block;background:#e8f8f0;color:#28AF60;padding:7px 18px;border-radius:20px;font-weight:700;font-size:15px;margin:14px 0;letter-spacing:.5px}
  .hint{font-size:12px;color:#aaa;margin-top:10px}

  /* Empty */
  .empty{text-align:center;padding:28px 16px;color:#bbb;font-size:13px}
  .empty span{font-size:28px;display:block;margin-bottom:8px}
</style>
</head>
<body>

<div class="header">
  <div class="header-inner">
    <div class="logo">🌿</div>
    <div>
      <h1>Siram Pintar</h1>
      <p>Konfigurasi koneksi WiFi perangkat IoT</p>
    </div>
  </div>
</div>

<div class="container">

  <!-- ═══ PANEL UTAMA ═══ -->
  <div id="mainPanel">
    <div class="card">
      <div class="tabs">
        <button class="tab active" id="tabScan" onclick="switchTab('scan')">📡 Scan WiFi</button>
        <button class="tab"        id="tabManual" onclick="switchTab('manual')">✏️ Manual</button>
      </div>

      <!-- Tab Scan -->
      <div id="pScan">
        <button class="scan-btn" id="scanBtn" onclick="doScan()">
          <span id="scanIcon">🔍</span>
          <span id="scanText">Scan Jaringan WiFi</span>
        </button>
        <div id="netList" class="net-list">
          <div class="empty"><span>📡</span>Tekan "Scan" untuk mencari jaringan di sekitar</div>
        </div>
      </div>

      <!-- Tab Manual -->
      <div id="pManual" class="hidden">
        <div class="field">
          <label>Nama WiFi (SSID)</label>
          <input type="text" id="manualSSID" placeholder="Contoh: RumahKu_2.4G">
        </div>
      </div>

      <!-- Password section (muncul setelah pilih jaringan / manual) -->
      <div id="passSection" class="pass-section hidden">
        <div id="selectedBadge" class="selected-badge hidden">
          📶 <span id="selectedLabel"></span>
        </div>
        <div class="field">
          <label>Password WiFi</label>
          <div class="pass-wrap">
            <input type="password" id="wifiPass" placeholder="Masukkan password WiFi" autocomplete="new-password">
            <button class="eye-btn" onclick="togglePass()">👁</button>
          </div>
        </div>
        <button class="btn btn-primary" id="connectBtn" onclick="doConnect()">
          Hubungkan
        </button>
        <button class="btn btn-outline" onclick="resetForm()">← Pilih Jaringan Lain</button>
        <div id="alertBox" class="hidden"></div>
      </div>
    </div>

    <div class="card" style="padding:12px 16px;text-align:center">
      <p style="font-size:12px;color:#aaa">
        📌 Jika halaman tidak terbuka otomatis, buka browser dan ketik
        <strong style="color:#28AF60">192.168.4.1</strong>
      </p>
    </div>
  </div>

  <!-- ═══ PANEL SEDANG TERHUBUNG ═══ -->
  <div id="connectingPanel" class="card hidden">
    <div class="connect-panel">
      <div class="big-spinner"></div>
      <h3>Menghubungkan ke WiFi…</h3>
      <p id="connectingSSID" style="color:#28AF60;font-weight:600;margin:6px 0 4px"></p>
      <p>Harap tunggu, proses ini memakan waktu<br>sekitar 10–15 detik</p>
    </div>
  </div>

  <!-- ═══ PANEL BERHASIL ═══ -->
  <div id="successPanel" class="card hidden">
    <div class="result-panel success">
      <div class="result-icon">✅</div>
      <h2>Berhasil Terhubung!</h2>
      <p>Perangkat Siram Pintar berhasil terhubung ke jaringan WiFi</p>
      <div class="ip-chip" id="deviceIP">—</div>
      <p>Perangkat akan <strong>restart otomatis</strong> dalam beberapa detik dan mulai memantau tanaman Anda 🌱</p>
      <p class="hint">Anda bisa menutup halaman ini</p>
    </div>
  </div>

  <!-- ═══ PANEL GAGAL ═══ -->
  <div id="errorPanel" class="card hidden">
    <div class="result-panel error">
      <div class="result-icon">❌</div>
      <h2>Koneksi Gagal</h2>
      <p id="errorMsg">Periksa password WiFi dan coba lagi</p>
      <br>
      <button class="btn btn-primary" onclick="retryConn()" style="max-width:280px;margin:0 auto">
        Coba Lagi
      </button>
    </div>
  </div>

</div><!-- /container -->

<script>
var selSSID='', selOpen=false, activeTab='scan';

function switchTab(t){
  activeTab=t;
  document.getElementById('tabScan').classList.toggle('active',t==='scan');
  document.getElementById('tabManual').classList.toggle('active',t==='manual');
  document.getElementById('pScan').classList.toggle('hidden',t!=='scan');
  document.getElementById('pManual').classList.toggle('hidden',t!=='manual');
  if(t==='manual'){
    document.getElementById('passSection').classList.remove('hidden');
    document.getElementById('selectedBadge').classList.add('hidden');
  } else {
    if(!selSSID) document.getElementById('passSection').classList.add('hidden');
  }
  hideAlert();
}

function doScan(){
  var btn=document.getElementById('scanBtn');
  btn.disabled=true;
  document.getElementById('scanIcon').textContent='⏳';
  document.getElementById('scanText').textContent='Sedang scan…';
  document.getElementById('netList').innerHTML='<div class="empty"><div class="spinner green" style="margin:0 auto 10px"></div>Mencari jaringan WiFi…</div>';

  fetch('/scan')
    .then(function(r){return r.json();})
    .then(function(nets){
      btn.disabled=false;
      document.getElementById('scanIcon').textContent='🔄';
      document.getElementById('scanText').textContent='Scan Ulang';
      renderNets(nets);
    })
    .catch(function(){
      btn.disabled=false;
      document.getElementById('scanIcon').textContent='🔍';
      document.getElementById('scanText').textContent='Scan Jaringan WiFi';
      document.getElementById('netList').innerHTML='<div class="empty"><span>⚠️</span>Gagal scan. Coba lagi.</div>';
    });
}

function renderNets(nets){
  var el=document.getElementById('netList');
  if(!nets||nets.length===0){
    el.innerHTML='<div class="empty"><span>😕</span>Tidak ada jaringan ditemukan</div>';
    return;
  }
  nets.sort(function(a,b){return b.rssi-a.rssi;});
  el.innerHTML=nets.map(function(n){
    return '<div class="net-item'+(n.ssid===selSSID?' selected':'')+'" onclick="selectNet(this,\''+esc(n.ssid)+'\','+(!n.secure)+')">'
      +'<span class="net-name">'+esc(n.ssid)+'</span>'
      +'<span class="net-meta">'+bars(n.rssi)+'<span class="lock">'+(n.secure?'🔒':'🔓')+'</span></span>'
      +'</div>';
  }).join('');
}

function bars(rssi){
  var lvl=rssi>-50?4:rssi>-60?3:rssi>-70?2:1;
  var h=[7,10,13,16]; var s='<div class="signal">';
  for(var i=1;i<=4;i++) s+='<div class="bar'+(i<=lvl?' on':'')+'" style="height:'+h[i-1]+'px"></div>';
  return s+'</div>';
}

function selectNet(el,ssid,isOpen){
  selSSID=ssid; selOpen=isOpen;
  document.querySelectorAll('.net-item').forEach(function(x){x.classList.remove('selected');});
  el.classList.add('selected');
  document.getElementById('selectedLabel').textContent=ssid;
  document.getElementById('selectedBadge').classList.remove('hidden');
  document.getElementById('passSection').classList.remove('hidden');
  var passInput=document.getElementById('wifiPass');
  if(isOpen){
    passInput.value='';
    passInput.placeholder='Jaringan terbuka — tidak perlu password';
    passInput.disabled=true;
  } else {
    passInput.placeholder='Masukkan password WiFi';
    passInput.disabled=false;
    passInput.value='';
    setTimeout(function(){passInput.focus();},100);
  }
  hideAlert();
}

function doConnect(){
  var ssid=activeTab==='manual'
    ?document.getElementById('manualSSID').value.trim()
    :selSSID;
  var pass=document.getElementById('wifiPass').value;

  if(!ssid){showAlert('Pilih atau masukkan nama WiFi terlebih dahulu');return;}

  // Tampilkan panel loading
  show('connectingPanel'); hide('mainPanel');
  document.getElementById('connectingSSID').textContent=ssid;

  var fd=new FormData();
  fd.append('ssid',ssid);
  fd.append('password',pass);

  fetch('/connect',{method:'POST',body:fd})
    .then(function(r){return r.json();})
    .then(function(d){
      hide('connectingPanel');
      if(d.success){
        document.getElementById('deviceIP').textContent=d.ip||'—';
        show('successPanel');
      } else {
        document.getElementById('errorMsg').textContent=d.message||'Gagal terhubung. Periksa password WiFi.';
        show('errorPanel');
      }
    })
    .catch(function(){
      hide('connectingPanel');
      document.getElementById('errorMsg').textContent='Tidak dapat berkomunikasi dengan perangkat. Coba lagi.';
      show('errorPanel');
    });
}

function retryConn(){hide('errorPanel');show('mainPanel');resetForm();}
function resetForm(){
  selSSID=''; selOpen=false;
  document.getElementById('passSection').classList.add('hidden');
  document.getElementById('selectedBadge').classList.add('hidden');
  document.getElementById('wifiPass').value='';
  document.getElementById('wifiPass').disabled=false;
  hideAlert();
}
function togglePass(){
  var i=document.getElementById('wifiPass');
  i.type=i.type==='password'?'text':'password';
}
function showAlert(msg){
  var el=document.getElementById('alertBox');
  el.className='alert alert-error';
  el.innerHTML='⚠️ '+msg;
  el.classList.remove('hidden');
}
function hideAlert(){
  var el=document.getElementById('alertBox');
  if(el) el.classList.add('hidden');
}
function show(id){document.getElementById(id).classList.remove('hidden');}
function hide(id){document.getElementById(id).classList.add('hidden');}
function esc(s){
  return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')
          .replace(/"/g,'&quot;').replace(/'/g,'&#39;');
}

// Auto scan saat pertama buka
window.onload=function(){doScan();};
</script>
</body>
</html>
)rawliteral";
