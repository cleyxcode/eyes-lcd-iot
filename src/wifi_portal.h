#pragma once
#include <pgmspace.h>

const char WIFI_PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Siram Pintar - Config</title>
<style>
  :root { --p: #2ecc71; --bg: #f8f9fa; --txt: #2d3436; }
  body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: var(--bg); color: var(--txt); margin: 0; padding: 20px; display: flex; flex-direction: column; align-items: center; }
  .card { background: white; padding: 25px; border-radius: 15px; box-shadow: 0 4px 15px rgba(0,0,0,0.1); width: 100%; max-width: 350px; margin-bottom: 20px; }
  h2 { margin-top: 0; color: var(--p); font-weight: 600; text-align: center; }
  input { width: 100%; padding: 12px; margin: 10px 0; border: 1px solid #ddd; border-radius: 8px; box-sizing: border-box; font-size: 14px; }
  button { width: 100%; padding: 12px; background: var(--p); color: white; border: none; border-radius: 8px; font-weight: bold; cursor: pointer; transition: opacity 0.2s; font-size: 14px; }
  button:active { opacity: 0.8; }
  .scan-btn { background: #3498db; margin-bottom: 15px; }
  #list { margin: 10px 0; max-height: 150px; overflow-y: auto; border: 1px solid #eee; border-radius: 8px; }
  .net { padding: 10px; border-bottom: 1px solid #eee; cursor: pointer; font-size: 14px; }
  .net:hover { background: #f1f1f1; }
  .net:last-child { border-bottom: none; }
  #status { margin-top: 15px; font-weight: bold; text-align: center; color: #e67e22; }
</style>
</head>
<body>

<div class="card">
  <h2>🌿 Siram Pintar</h2>
  <button class="scan-btn" onclick="scan()">Cari WiFi</button>
  <div id="list"></div>
  
  <input id="ssid" placeholder="Nama WiFi (SSID)">
  <input id="pass" type="password" placeholder="Password WiFi">
  
  <button onclick="connect()">Simpan & Hubungkan</button>
  <div id="status"></div>
</div>

<script>
function scan(){
  const list = document.getElementById('list');
  list.innerHTML = "Scanning...";
  fetch('/scan').then(r=>r.json()).then(data=>{
    let html="";
    data.forEach(n=>{
      html+=`<div class="net" onclick="pilih('${n.ssid}')">${n.ssid} (${n.rssi}dBm)</div>`;
    });
    list.innerHTML=html || "Tidak ada WiFi ditemukan";
  });
}

function pilih(s){
  document.getElementById('ssid').value=s;
}

function connect(){
  let ssid = document.getElementById('ssid').value;
  let pass = document.getElementById('pass').value;
  
  if(!ssid) {
    alert("SSID wajib diisi!");
    return;
  }

  let fd = new FormData();
  fd.append("ssid", ssid);
  fd.append("password", pass);

  document.getElementById('status').innerHTML = "Menyubungkan...";
  
  fetch('/connect', {method:'POST', body:fd})
  .then(r=>r.json())
  .then(d=>{
    document.getElementById('status').innerHTML = d.message;
    if(d.status === "ok") {
      setTimeout(() => {
        document.body.innerHTML = "<h1>Konfigurasi Disimpan!</h1><p>Alat akan restart dan mencoba terhubung. Tutup halaman ini.</p>";
      }, 2000);
    }
  });
}
</script>

</body>
</html>
)rawliteral";