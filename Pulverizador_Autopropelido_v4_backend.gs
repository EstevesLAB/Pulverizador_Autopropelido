// ======================================================================================
// Pulverizador Autopropelido
// Receive data from ESP32, write in google sheets and display on a dashboard (Web App)
Authors: Igor Esteves e Rafael Favalli
// ======================================================================================

// =======================================================
// CONFIGURAÇÕES
// =======================================================

var TOKEN_ESPERADO = "My_Token";

var ABA_LEITURAS = "leituras";
var ABA_EVENTOS = "eventos";
var ABA_CONFIGURACAO = "configuracao";

// =======================================================
// HTTP GET
// =======================================================

function doGet(e) {
  try {
    inicializarEstrutura();

    var action = "";
    var token = "";
    var limit = 120;

    if (e && e.parameter) {
      action = e.parameter.action || "";
      token = e.parameter.token || "";
      limit = Number(e.parameter.limit || 120);
    }

    if (action === "config") {
      validarToken(token);
      return responderJson(obterConfiguracao());
    }

    if (action === "leituras") {
      validarToken(token);
      return responderJson({
        status: "ok",
        dados: getUltimasLeituras(limit)
      });
    }

    if (action === "eventos") {
      validarToken(token);
      return responderJson({
        status: "ok",
        dados: getEventosRecentes(limit)
      });
    }

    return HtmlService
      .createHtmlOutput(DASHBOARD_HTML)
      .setTitle("Dashboard Pulverizador")
      .setXFrameOptionsMode(HtmlService.XFrameOptionsMode.ALLOWALL);

  } catch (erro) {
    return responderJson({
      status: "erro",
      origem: "doGet",
      mensagem: String(erro)
    });
  }
}

// =======================================================
// HTTP POST
// =======================================================

function doPost(e) {
  try {
    inicializarEstrutura();

    if (!e || !e.postData || !e.postData.contents) {
      return responderJson({
        status: "erro",
        mensagem: "Requisicao sem corpo"
      });
    }

    var dados = JSON.parse(e.postData.contents);

    validarToken(dados.token || "");

    var ss = SpreadsheetApp.getActiveSpreadsheet();
    var abaLeituras = ss.getSheetByName(ABA_LEITURAS);
    var abaEventos = ss.getSheetByName(ABA_EVENTOS);

    var timestamp = new Date();

    gravarLeitura(abaLeituras, timestamp, dados);
    var eventosRecebidos = gravarEventos(abaEventos, timestamp, dados);

    return responderJson({
      status: "ok",
      mensagem: "Dados gravados com sucesso",
      eventos_recebidos: eventosRecebidos
    });

  } catch (erro) {
    return responderJson({
      status: "erro",
      origem: "doPost",
      mensagem: String(erro)
    });
  }
}

// =======================================================
// SEGURANÇA
// =======================================================

function validarToken(token) {
  if (token !== TOKEN_ESPERADO) {
    throw new Error("Token invalido");
  }
}

// =======================================================
// INICIALIZAÇÃO
// =======================================================

function inicializarEstrutura() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();

  obterOuCriarAba(ss, ABA_LEITURAS, [
    "timestamp_recebimento",
    "device_id",
    "millis_esp32",
    "distancia_cm",
    "pitch_deg",
    "roll_deg",
    "aceleracao_resultante_g",
    "vibracao_rms_g",
    "vibracao_pico_g",
    "alarme_distancia",
    "alarme_inclinacao",
    "alarme_vibracao",
    "rssi_wifi"
  ]);

  obterOuCriarAba(ss, ABA_EVENTOS, [
    "timestamp_recebimento",
    "device_id",
    "tipo_evento",
    "estado_anterior",
    "estado_atual",
    "valor",
    "mensagem"
  ]);

  var abaConfig = obterOuCriarAba(ss, ABA_CONFIGURACAO, [
    "parametro",
    "valor",
    "unidade",
    "descricao"
  ]);

  garantirConfiguracaoPadrao(abaConfig);
}

function obterOuCriarAba(ss, nome, cabecalhos) {
  var aba = ss.getSheetByName(nome);

  if (!aba) {
    aba = ss.insertSheet(nome);
  }

  if (aba.getLastRow() === 0) {
    aba.getRange(1, 1, 1, cabecalhos.length).setValues([cabecalhos]);
  }

  return aba;
}

function garantirConfiguracaoPadrao(aba) {
  if (aba.getLastRow() > 1) {
    return;
  }

  var linhas = [
    ["limite_distancia_baixa_cm", 10, "cm", "Distancia minima aceitavel da barra ao solo"],
    ["limite_distancia_alta_cm", 20, "cm", "Distancia maxima aceitavel da barra ao solo"],
    ["histerese_distancia_cm", 5, "cm", "Histerese para alarme de distancia"],

    ["limite_inclinacao_alerta_deg", 20, "graus", "Limite de alerta para pitch ou roll"],
    ["limite_inclinacao_critica_deg", 40, "graus", "Limite critico para pitch ou roll"],
    ["histerese_inclinacao_deg", 1, "graus", "Histerese para alarme de inclinacao"],

    ["limite_vibracao_alerta_g", 0.05, "g", "Limite de alerta RMS de vibracao"],
    ["limite_vibracao_critica_g", 0.15, "g", "Limite critico RMS de vibracao"],
    ["histerese_vibracao_g", 0.01, "g", "Histerese para alarme de vibracao"],

    ["alpha_complementar", 0.98, "-", "Peso do giroscopio no filtro complementar"],
    ["contagem_confirmacao_alarme", 3, "leituras", "Quantidade de confirmacoes antes de mudar estado"]
  ];

  aba.getRange(2, 1, linhas.length, 4).setValues(linhas);
}

// =======================================================
// CONFIGURAÇÃO
// =======================================================

function obterConfiguracao() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var aba = ss.getSheetByName(ABA_CONFIGURACAO);

  var valores = aba.getDataRange().getValues();

  var config = {
    status: "ok"
  };

  for (var i = 1; i < valores.length; i++) {
    var parametro = valores[i][0];
    var valor = valores[i][1];

    if (!parametro) continue;

    var numero = Number(valor);

    if (!isNaN(numero) && valor !== "") {
      config[parametro] = numero;
    } else {
      config[parametro] = valor;
    }
  }

  return config;
}

// =======================================================
// GRAVAÇÃO
// =======================================================

function gravarLeitura(abaLeituras, timestamp, dados) {
  abaLeituras.appendRow([
    timestamp,
    dados.device_id || "",
    dados.millis_esp32 || "",
    valorOuVazio(dados.distancia_cm),
    valorOuVazio(dados.pitch_deg),
    valorOuVazio(dados.roll_deg),
    valorOuVazio(dados.aceleracao_resultante_g),
    valorOuVazio(dados.vibracao_rms_g),
    valorOuVazio(dados.vibracao_pico_g),
    dados.alarme_distancia || "",
    dados.alarme_inclinacao || "",
    dados.alarme_vibracao || "",
    valorOuVazio(dados.rssi_wifi)
  ]);
}

function gravarEventos(abaEventos, timestamp, dados) {
  var eventosRecebidos = 0;

  if (!dados.eventos) {
    return eventosRecebidos;
  }

  if (Object.prototype.toString.call(dados.eventos) !== "[object Array]") {
    return eventosRecebidos;
  }

  for (var i = 0; i < dados.eventos.length; i++) {
    var evento = dados.eventos[i];

    abaEventos.appendRow([
      timestamp,
      dados.device_id || "",
      evento.tipo || "",
      evento.estado_anterior || "",
      evento.estado_atual || "",
      valorOuVazio(evento.valor),
      "Mudanca de estado: " +
        (evento.estado_anterior || "") +
        " -> " +
        (evento.estado_atual || "")
    ]);

    eventosRecebidos++;
  }

  return eventosRecebidos;
}

// =======================================================
// CONSULTAS PARA DASHBOARD
// =======================================================


function getEventosRecentes(limite) {
  inicializarEstrutura();

  limite = Number(limite || 50);

  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var aba = ss.getSheetByName(ABA_EVENTOS);

  var ultimaLinha = aba.getLastRow();
  var ultimaColuna = aba.getLastColumn();

  if (ultimaLinha <= 1) {
    return [];
  }

  var inicio = Math.max(2, ultimaLinha - limite + 1);
  var quantidade = ultimaLinha - inicio + 1;

  var cabecalhos = aba.getRange(1, 1, 1, ultimaColuna).getValues()[0];
  var linhas = aba.getRange(inicio, 1, quantidade, ultimaColuna).getValues();

  return linhas.map(function(linha) {
    return linhaParaObjeto(cabecalhos, linha);
  });
}

function getUltimasLeituras(limite) {
  inicializarEstrutura();

  limite = Number(limite || 120);

  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var aba = ss.getSheetByName(ABA_LEITURAS);

  var ultimaLinha = aba.getLastRow();
  var ultimaColuna = aba.getLastColumn();

  if (ultimaLinha <= 1) {
    return [];
  }

  var inicio = Math.max(2, ultimaLinha - limite + 1);
  var quantidade = ultimaLinha - inicio + 1;

  var cabecalhos = aba.getRange(1, 1, 1, ultimaColuna).getValues()[0];
  var linhas = aba.getRange(inicio, 1, quantidade, ultimaColuna).getValues();

  var dados = [];

  for (var i = 0; i < linhas.length; i++) {
    var obj = linhaParaObjeto(cabecalhos, linhas[i]);
    dados.push(normalizarLeitura(obj));
  }

  return dados;
}

function linhaParaObjeto(cabecalhos, linha) {
  var obj = {};

  for (var i = 0; i < cabecalhos.length; i++) {
    var chave = String(cabecalhos[i] || "").trim();
    var valor = linha[i];

    if (!chave) {
      continue;
    }

    if (valor instanceof Date) {
      obj[chave] = valor.toISOString();
    } else {
      obj[chave] = valor;
    }
  }

  return obj;
}

function normalizarLeitura(obj) {
  return {
    timestamp_recebimento:
      obj.timestamp_recebimento ||
      obj.timestamp ||
      obj.data_hora ||
      obj.data ||
      "",

    device_id:
      obj.device_id ||
      obj.dispositivo ||
      "",

    millis_esp32:
      obj.millis_esp32 ||
      obj.millis ||
      "",

    distancia_cm:
      obj.distancia_cm ||
      obj.distancia_filtrada_cm ||
      obj.distancia ||
      "",

    pitch_deg:
      obj.pitch_deg ||
      obj.pitch ||
      "",

    roll_deg:
      obj.roll_deg ||
      obj.roll ||
      "",

    aceleracao_resultante_g:
      obj.aceleracao_resultante_g ||
      obj.acc_res_g ||
      obj.acc_resultante_g ||
      "",

    vibracao_rms_g:
      obj.vibracao_rms_g ||
      obj.vib_rms_g ||
      obj.vibracao_rms ||
      "",

    vibracao_pico_g:
      obj.vibracao_pico_g ||
      obj.vib_pico_g ||
      obj.vibracao_pico ||
      "",

    alarme_distancia:
      obj.alarme_distancia ||
      obj.alarme_dist ||
      "",

    alarme_inclinacao:
      obj.alarme_inclinacao ||
      obj.alarme_inc ||
      "",

    alarme_vibracao:
      obj.alarme_vibracao ||
      obj.alarme_vib ||
      "",

    rssi_wifi:
      obj.rssi_wifi ||
      obj.rssi ||
      ""
  };
}

// =======================================================
// UTILITÁRIOS
// =======================================================

function valorOuVazio(valor) {
  if (valor === null || valor === undefined) {
    return "";
  }

  return valor;
}

function responderJson(objeto) {
  return ContentService
    .createTextOutput(JSON.stringify(objeto))
    .setMimeType(ContentService.MimeType.JSON);
}

// =======================================================
// DASHBOARD HTML
// =======================================================

var DASHBOARD_HTML = `
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<title>Dashboard Pulverizador</title>

<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>

<style>
:root{
  --bg:#0f172a;
  --card:#111827;
  --border:#374151;
  --text:#e5e7eb;
  --muted:#9ca3af;
  --green:#22c55e;
  --yellow:#facc15;
  --red:#ef4444;
  --blue:#38bdf8;
  --purple:#a78bfa;
}

*{
  box-sizing:border-box;
}

html, body{
  width:100%;
  height:100%;
  margin:0;
  overflow:hidden;
}

body{
  font-family:Arial, Helvetica, sans-serif;
  background:var(--bg);
  color:var(--text);
}

header{
  height:70px;
  text-align:center;
  padding:10px 12px 8px;
  border-bottom:1px solid var(--border);
}

header h1{
  margin:0;
  font-size:22px;
  line-height:28px;
}

header p{
  margin:4px 0 0;
  font-size:12px;
  color:var(--muted);
}

main{
  width:100%;
  height:calc(100vh - 70px);
  max-width:1366px;
  margin:0 auto;
  padding:10px 14px 8px;
  display:flex;
  flex-direction:column;
  gap:8px;
}

.grid{
  display:grid;
  gap:8px;
}

.cards{
  grid-template-columns:repeat(6,1fr);
  height:78px;
}

.alarms{
  grid-template-columns:repeat(3,1fr);
  height:66px;
}

.card{
  background:var(--card);
  border:1px solid var(--border);
  border-radius:10px;
  padding:8px 10px;
  min-width:0;
}

.card h2{
  font-size:11px;
  line-height:14px;
  color:var(--muted);
  margin:0 0 5px;
  font-weight:normal;
}

.value{
  font-size:22px;
  line-height:26px;
  font-weight:bold;
}

.unit{
  font-size:12px;
  color:var(--muted);
  margin-left:3px;
}

.status{
  font-size:19px;
  line-height:22px;
  font-weight:bold;
  padding:8px;
  border-radius:8px;
  text-align:center;
}

.normal{
  background:rgba(34,197,94,0.18);
  color:var(--green);
  border:1px solid rgba(34,197,94,0.45);
}

.alerta{
  background:rgba(250,204,21,0.18);
  color:var(--yellow);
  border:1px solid rgba(250,204,21,0.45);
}

.critico,
.BAIXA,
.ALTA{
  background:rgba(239,68,68,0.18);
  color:var(--red);
  border:1px solid rgba(239,68,68,0.45);
}

.charts{
  flex:1;
  min-height:0;
  display:grid;
  grid-template-columns:repeat(2,1fr);
  grid-template-rows:repeat(2,1fr);
  gap:8px;
}

.chart-box{
  background:var(--card);
  border:1px solid var(--border);
  border-radius:10px;
  padding:8px 10px 6px;
  min-height:0;
  display:flex;
  flex-direction:column;
}

.chart-box h2{
  font-size:14px;
  line-height:18px;
  margin:0 0 4px;
}

.chart-holder{
  flex:1;
  min-height:0;
  position:relative;
}

canvas{
  width:100% !important;
  height:100% !important;
}

.footer{
  height:14px;
  text-align:center;
  color:var(--muted);
  font-size:10px;
  line-height:14px;
}

@media(max-width:1000px){
  html, body{
    overflow:auto;
    height:auto;
  }

  main{
    height:auto;
  }

  .cards{
    grid-template-columns:repeat(2,1fr);
    height:auto;
  }

  .alarms{
    grid-template-columns:1fr;
    height:auto;
  }

  .charts{
    grid-template-columns:1fr;
    grid-template-rows:auto;
  }

  .chart-holder{
    height:230px;
  }
}
</style>
</head>

<body>

<header>
  <h1>Dashboard Local - Pulverizador Autopropelido</h1>
  <p>Monitoramento em tempo real</p>
</header>

<main>

<section class="grid cards">
  <div class="card">
    <h2>Distância ao solo</h2>
    <span class="value" id="dist">--</span><span class="unit">cm</span>
  </div>

  <div class="card">
    <h2>Pitch</h2>
    <span class="value" id="pitch">--</span><span class="unit">°</span>
  </div>

  <div class="card">
    <h2>Roll</h2>
    <span class="value" id="roll">--</span><span class="unit">°</span>
  </div>

  <div class="card">
    <h2>Vibração RMS</h2>
    <span class="value" id="rms">--</span><span class="unit">g</span>
  </div>

  <div class="card">
    <h2>Vibração Pico</h2>
    <span class="value" id="pico">--</span><span class="unit">g</span>
  </div>

  <div class="card">
    <h2>Acel. resultante</h2>
    <span class="value" id="acc">--</span><span class="unit">g</span>
  </div>
</section>

<section class="grid alarms">
  <div class="card">
    <h2>Alarme distância</h2>
    <div id="alarmDist" class="status">--</div>
  </div>

  <div class="card">
    <h2>Alarme inclinação</h2>
    <div id="alarmInc" class="status">--</div>
  </div>

  <div class="card">
    <h2>Alarme vibração</h2>
    <div id="alarmVib" class="status">--</div>
  </div>
</section>

<section class="charts">
  <div class="chart-box">
    <h2>Distância ao solo x tempo</h2>
    <div class="chart-holder">
      <canvas id="c1"></canvas>
    </div>
  </div>

  <div class="chart-box">
    <h2>Pitch e Roll x tempo</h2>
    <div class="chart-holder">
      <canvas id="c2"></canvas>
    </div>
  </div>

  <div class="chart-box">
    <h2>Vibração RMS x tempo</h2>
    <div class="chart-holder">
      <canvas id="c3"></canvas>
    </div>
  </div>

  <div class="chart-box">
    <h2>Aceleração resultante x tempo</h2>
    <div class="chart-holder">
      <canvas id="c4"></canvas>
    </div>
  </div>
</section>

<div class="footer">
  Atualização automática via Google Apps Script. Janela deslizante com os últimos pontos.
</div>

</main>

<script>
let c1;
let c2;
let c3;
let c4;

function n(v){
  if(v === null || v === "" || v === undefined || isNaN(v)) return null;
  return Number(v);
}

function fmt(v, casas){
  const x = n(v);
  if(x === null) return "--";
  return x.toFixed(casas);
}

function setStatus(el, v){
  el.textContent = v || "--";
  el.className = "status";

  let s = String(v || "").toLowerCase();

  if(s.includes("normal")){
    el.classList.add("normal");
  }else if(s.includes("alerta")){
    el.classList.add("alerta");
  }else{
    el.classList.add("critico");
  }
}

function chartOptions(){
  return {
    responsive:true,
    maintainAspectRatio:false,
    animation:false,
    spanGaps:true,
    elements:{
      point:{
        radius:0
      },
      line:{
        borderWidth:2
      }
    },
    plugins:{
      legend:{
        labels:{
          color:"#e5e7eb",
          boxWidth:16,
          font:{
            size:10
          }
        }
      }
    },
    scales:{
      x:{
        ticks:{
          color:"#9ca3af",
          maxTicksLimit:6,
          font:{
            size:9
          }
        },
        grid:{
          color:"rgba(55,65,81,0.35)"
        }
      },
      y:{
        ticks:{
          color:"#9ca3af",
          font:{
            size:9
          }
        },
        grid:{
          color:"rgba(55,65,81,0.55)"
        }
      }
    }
  };
}

function plot(chart, canvas, labels, datasets){
  if(chart){
    chart.data.labels = labels;
    chart.data.datasets = datasets;
    chart.update();
    return chart;
  }

  return new Chart(canvas,{
    type:"line",
    data:{
      labels:labels,
      datasets:datasets
    },
    options:chartOptions()
  });
}

function atualizar(dados){
  if(!dados || dados.length === 0) return;

  let last = dados[dados.length - 1];

  document.getElementById("dist").textContent = fmt(last.distancia_cm, 1);
  document.getElementById("pitch").textContent = fmt(last.pitch_deg, 2);
  document.getElementById("roll").textContent = fmt(last.roll_deg, 2);
  document.getElementById("rms").textContent = fmt(last.vibracao_rms_g, 4);
  document.getElementById("pico").textContent = fmt(last.vibracao_pico_g, 4);
  document.getElementById("acc").textContent = fmt(last.aceleracao_resultante_g, 4);

  setStatus(document.getElementById("alarmDist"), last.alarme_distancia);
  setStatus(document.getElementById("alarmInc"), last.alarme_inclinacao);
  setStatus(document.getElementById("alarmVib"), last.alarme_vibracao);

  let data = dados.slice(-120);

  let labels = data.map(x => {
    let dt = new Date(x.timestamp_recebimento);
    if(isNaN(dt.getTime())) return "";
    return dt.toLocaleTimeString("pt-BR");
  });

  c1 = plot(
    c1,
    document.getElementById("c1"),
    labels,
    [
      {
        label:"Distância",
        data:data.map(x => n(x.distancia_cm)),
        borderColor:"#38bdf8",
        backgroundColor:"rgba(56,189,248,0.1)"
      }
    ]
  );

  c2 = plot(
    c2,
    document.getElementById("c2"),
    labels,
    [
      {
        label:"Pitch",
        data:data.map(x => n(x.pitch_deg)),
        borderColor:"#a78bfa",
        backgroundColor:"rgba(167,139,250,0.1)"
      },
      {
        label:"Roll",
        data:data.map(x => n(x.roll_deg)),
        borderColor:"#facc15",
        backgroundColor:"rgba(250,204,21,0.1)"
      }
    ]
  );

  c3 = plot(
    c3,
    document.getElementById("c3"),
    labels,
    [
      {
        label:"RMS",
        data:data.map(x => n(x.vibracao_rms_g)),
        borderColor:"#ef4444",
        backgroundColor:"rgba(239,68,68,0.1)"
      }
    ]
  );

  c4 = plot(
    c4,
    document.getElementById("c4"),
    labels,
    [
      {
        label:"Aceleração",
        data:data.map(x => n(x.aceleracao_resultante_g)),
        borderColor:"#22c55e",
        backgroundColor:"rgba(34,197,94,0.1)"
      }
    ]
  );
}

function load(){
  google.script.run
    .withSuccessHandler(atualizar)
    .getUltimasLeituras(150);
}

setInterval(load, 5000);
load();
</script>

</body>
</html>
`;
