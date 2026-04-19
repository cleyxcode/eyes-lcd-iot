#pragma once
#include <pgmspace.h>
#include <Arduino.h>
const char WIFI_PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Siram Pintar</title>
<style>
body{font-family:sans-serif;text-align:center;background:#f0f0f0}
.box{background:white;padding:20px;margin:20px;border-radius:10px}
button{padding:10px;margin:5px}
input{padding:10px;width:80%}
</style>
</head>
<body>

<h2>🌿 Siram Pintar</h2>

<div class="box">
  <button onclick="scan()">Scan WiFi</button>
  <div id="list"></div>
</div>

<div class="box">
  <input id="ssid" placeholder="SSID"><br><br>
  <input id="pass" type="password" placeholder="Password"><br><br>
  <button onclick="connect()">Hubungkan</button>
</div>

<div id="status"></div>

<script>
function scan(){
  fetch('/scan')
  .then(r=>r.json())
  .then(data=>{
    let html="";
    data.forEach(n=>{
      html+=`<div onclick="pilih('${n.ssid}')">${n.ssid}</div>`;
    });
    document.getElementById('list').innerHTML=html;
  });
}

function pilih(s){
  document.getElementById('ssid').value=s;
}

function connect(){
  let ssid=document.getElementById('ssid').value;
  let pass=document.getElementById('pass').value;

  let fd=new FormData();
  fd.append("ssid",ssid);
  fd.append("password",pass);

  fetch('/connect',{method:'POST',body:fd})
  .then(r=>r.json())
  .then(d=>{
    document.getElementById('status').innerHTML=d.message;
  });
}
</script>

</body>
</html>
)rawliteral";