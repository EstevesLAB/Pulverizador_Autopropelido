// =======================================================
// Project: PULVERIZADOR AUTOPROPELIDO
// Hardware: ESP32
// Authors: Igor Esteves e Rafael Favalli
// =======================================================

#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <math.h>

// =======================================================
// CONFIGURAÇÕES PRINCIPAIS
// =======================================================

const char* WIFI_SSID = "WIFI_NETWORK";
const char* WIFI_PASSWORD = "PASSWORD";

const char* AP_SSID = "Pulverizador_ESP32"; //Personal network
const char* AP_PASSWORD = "12345678"; //password

const char* DEVICE_ID = "pulverizador_01";

const char* GOOGLE_SCRIPT_URL = "APPS_SCRIPTS_URL"; //URL to execute apps scripts code (data acq. and dashboard)
const char* TOKEN_API = "MY_TOKEN";

// =======================================================
// PINOS
// =======================================================

const int PINO_TRIG = 5;
const int PINO_ECHO = 18;

const int PINO_SDA = 21;
const int PINO_SCL = 22;

const int PINO_LED_ALARME = 2;

// =======================================================
// OBJETOS
// =======================================================

Adafruit_MPU6050 mpu;
WebServer server(80);

// =======================================================
// INTERVALOS
// =======================================================

const unsigned long INTERVALO_MPU_MS = 20;               // 50 Hz
const unsigned long INTERVALO_DISTANCIA_MS = 100;        // 10 Hz
const unsigned long INTERVALO_IMPRESSAO_MS = 1000;       // 1 Hz
const unsigned long INTERVALO_ENVIO_GOOGLE_MS = 5000;    // 5 s
const unsigned long INTERVALO_CONFIG_GOOGLE_MS = 60000;  // 60 s

unsigned long tempoAnteriorMPU = 0;
unsigned long tempoAnteriorDistancia = 0;
unsigned long tempoAnteriorImpressao = 0;

// =======================================================
// WI-FI / GOOGLE STATUS
// =======================================================

bool wifiConectadoComoStation = false;

bool ultimoEnvioGoogleOk = false;
int ultimoHttpCodeGoogle = 0;
String ultimoErroGoogle = "Aguardando primeiro envio";
unsigned long ultimoEnvioGoogleMillis = 0;

bool ultimaConfigGoogleOk = false;
String ultimoErroConfigGoogle = "Aguardando primeira leitura";
unsigned long ultimaConfigGoogleMillis = 0;

// =======================================================
// DISTÂNCIA
// =======================================================

const int TAM_FILTRO_DISTANCIA = 10;
float bufferDistancia[TAM_FILTRO_DISTANCIA];
int indiceDistancia = 0;
bool bufferDistanciaCheio = false;

float distanciaCm = NAN;
float distanciaFiltradaCm = NAN;

// =======================================================
// MPU-6050 / FILTRO COMPLEMENTAR
// =======================================================

float ALPHA_COMPLEMENTAR = 0.98;

float pitchFiltradoDeg = 0.0;
float rollFiltradoDeg = 0.0;
bool filtroComplementarInicializado = false;

float offsetAx = 0.0;
float offsetAy = 0.0;
float offsetAz = 0.0;
float offsetGx = 0.0;
float offsetGy = 0.0;
float offsetGz = 0.0;

float ax_g = 0.0;
float ay_g = 0.0;
float az_g = 0.0;

float gx_dps = 0.0;
float gy_dps = 0.0;
float gz_dps = 0.0;

float pitchAccDeg = 0.0;
float rollAccDeg = 0.0;

float aceleracaoResultanteG = 0.0;
float vibracaoInstantaneaG = 0.0;

// =======================================================
// VIBRAÇÃO
// =======================================================

const int TAM_JANELA_VIBRACAO = 50;

float somaQuadradosVibracao = 0.0;
float picoVibracaoG = 0.0;
int contadorVibracao = 0;

float vibracaoRmsG = 0.0;
float vibracaoPicoG = 0.0;

bool novaJanelaVibracaoDisponivel = false;

// =======================================================
// LIMITES CONFIGURÁVEIS
// =======================================================

float LIMITE_DISTANCIA_BAIXA_CM = 10.0;
float LIMITE_DISTANCIA_ALTA_CM = 20.0;
float HISTERESE_DISTANCIA_CM = 5.0;

float LIMITE_INCLINACAO_ALERTA_DEG = 20.0;
float LIMITE_INCLINACAO_CRITICA_DEG = 40.0;
float HISTERESE_INCLINACAO_DEG = 1.0;

float LIMITE_VIBRACAO_ALERTA_G = 0.05;
float LIMITE_VIBRACAO_CRITICA_G = 0.15;
float HISTERESE_VIBRACAO_G = 0.01;

int CONTAGEM_CONFIRMACAO_ALARME = 3;

// =======================================================
// ESTADOS DE ALARME
// =======================================================

enum EstadoDistancia {
  DISTANCIA_NORMAL,
  DISTANCIA_BAIXA,
  DISTANCIA_ALTA,
  DISTANCIA_INVALIDA
};

enum EstadoNivelAlarme {
  ALARME_NORMAL,
  ALARME_ALERTA,
  ALARME_CRITICO
};

EstadoDistancia estadoDistancia = DISTANCIA_NORMAL;
EstadoNivelAlarme estadoInclinacao = ALARME_NORMAL;
EstadoNivelAlarme estadoVibracao = ALARME_NORMAL;

int contadorAlarmeDistanciaBaixa = 0;
int contadorAlarmeDistanciaAlta = 0;
int contadorAlarmeDistanciaInvalida = 0;

int contadorAlarmeInclinacaoAlerta = 0;
int contadorAlarmeInclinacaoCritica = 0;

int contadorAlarmeVibracaoAlerta = 0;
int contadorAlarmeVibracaoCritica = 0;

// Estados confirmados como enviados ao Google
String ultimoEstadoDistanciaEnviado = "";
String ultimoEstadoInclinacaoEnviado = "";
String ultimoEstadoVibracaoEnviado = "";

// =======================================================
// FUNÇÕES AUXILIARES
// =======================================================

String jsonFloat(float valor, int casasDecimais) {
  if (isnan(valor) || isinf(valor)) return "null";
  return String(valor, casasDecimais);
}

String jsonString(String texto) {
  texto.replace("\\", "\\\\");
  texto.replace("\"", "\\\"");
  texto.replace("\n", " ");
  texto.replace("\r", " ");
  return "\"" + texto + "\"";
}

String textoEstadoDistancia(EstadoDistancia estado) {
  switch (estado) {
    case DISTANCIA_NORMAL: return "NORMAL";
    case DISTANCIA_BAIXA: return "BAIXA";
    case DISTANCIA_ALTA: return "ALTA";
    case DISTANCIA_INVALIDA: return "INVALIDA";
    default: return "DESCONHECIDO";
  }
}

String textoEstadoNivel(EstadoNivelAlarme estado) {
  switch (estado) {
    case ALARME_NORMAL: return "NORMAL";
    case ALARME_ALERTA: return "ALERTA";
    case ALARME_CRITICO: return "CRITICO";
    default: return "DESCONHECIDO";
  }
}

float extrairFloatJson(String json, String chave, float valorPadrao) {
  String busca = "\"" + chave + "\":";
  int pos = json.indexOf(busca);
  if (pos < 0) return valorPadrao;

  pos += busca.length();

  int fimVirgula = json.indexOf(",", pos);
  int fimChave = json.indexOf("}", pos);

  int fim = -1;

  if (fimVirgula >= 0 && fimChave >= 0) {
    fim = min(fimVirgula, fimChave);
  } else if (fimVirgula >= 0) {
    fim = fimVirgula;
  } else {
    fim = fimChave;
  }

  if (fim < 0) return valorPadrao;

  String valor = json.substring(pos, fim);
  valor.trim();
  valor.replace("\"", "");

  if (valor == "null" || valor.length() == 0) return valorPadrao;

  return valor.toFloat();
}

int extrairIntJson(String json, String chave, int valorPadrao) {
  return (int)extrairFloatJson(json, chave, valorPadrao);
}

// =======================================================
// HC-SR04
// =======================================================

float lerDistanciaCm() {
  digitalWrite(PINO_TRIG, LOW);
  delayMicroseconds(2);

  digitalWrite(PINO_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PINO_TRIG, LOW);

  long duracao = pulseIn(PINO_ECHO, HIGH, 30000);

  if (duracao == 0) return NAN;

  float distancia = duracao * 0.0343 / 2.0;

  if (distancia < 2.0 || distancia > 400.0) return NAN;

  return distancia;
}

float filtrarDistancia(float novaDistancia) {
  if (isnan(novaDistancia)) return distanciaFiltradaCm;

  bufferDistancia[indiceDistancia] = novaDistancia;
  indiceDistancia++;

  if (indiceDistancia >= TAM_FILTRO_DISTANCIA) {
    indiceDistancia = 0;
    bufferDistanciaCheio = true;
  }

  int tamanhoAtual = bufferDistanciaCheio ? TAM_FILTRO_DISTANCIA : indiceDistancia;

  float soma = 0.0;
  for (int i = 0; i < tamanhoAtual; i++) {
    soma += bufferDistancia[i];
  }

  return soma / tamanhoAtual;
}

// =======================================================
// MPU-6050
// =======================================================

void calibrarMPU6050(int numeroAmostras) {
  Serial.println();
  Serial.println("Calibrando MPU-6050...");
  Serial.println("Mantenha o sensor parado e nivelado.");

  float somaAx = 0.0;
  float somaAy = 0.0;
  float somaAz = 0.0;
  float somaGx = 0.0;
  float somaGy = 0.0;
  float somaGz = 0.0;

  sensors_event_t a, g, temp;

  for (int i = 0; i < numeroAmostras; i++) {
    mpu.getEvent(&a, &g, &temp);

    somaAx += a.acceleration.x;
    somaAy += a.acceleration.y;
    somaAz += a.acceleration.z;

    somaGx += g.gyro.x;
    somaGy += g.gyro.y;
    somaGz += g.gyro.z;

    delay(10);
  }

  offsetAx = somaAx / numeroAmostras;
  offsetAy = somaAy / numeroAmostras;
  offsetAz = (somaAz / numeroAmostras) - 9.80665;

  offsetGx = somaGx / numeroAmostras;
  offsetGy = somaGy / numeroAmostras;
  offsetGz = somaGz / numeroAmostras;

  Serial.println("Calibracao concluida.");
  Serial.println();
}

void processarMPU6050(float dt) {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  float ax_ms2 = a.acceleration.x - offsetAx;
  float ay_ms2 = a.acceleration.y - offsetAy;
  float az_ms2 = a.acceleration.z - offsetAz;

  float gx_rad_s = g.gyro.x - offsetGx;
  float gy_rad_s = g.gyro.y - offsetGy;
  float gz_rad_s = g.gyro.z - offsetGz;

  ax_g = ax_ms2 / 9.80665;
  ay_g = ay_ms2 / 9.80665;
  az_g = az_ms2 / 9.80665;

  gx_dps = gx_rad_s * 180.0 / PI;
  gy_dps = gy_rad_s * 180.0 / PI;
  gz_dps = gz_rad_s * 180.0 / PI;

  rollAccDeg = atan2(ay_g, az_g) * 180.0 / PI;
  pitchAccDeg = atan2(-ax_g, sqrt(ay_g * ay_g + az_g * az_g)) * 180.0 / PI;

  if (!filtroComplementarInicializado) {
    pitchFiltradoDeg = pitchAccDeg;
    rollFiltradoDeg = rollAccDeg;
    filtroComplementarInicializado = true;
  }

  rollFiltradoDeg =
    ALPHA_COMPLEMENTAR * (rollFiltradoDeg + gx_dps * dt) +
    (1.0 - ALPHA_COMPLEMENTAR) * rollAccDeg;

  pitchFiltradoDeg =
    ALPHA_COMPLEMENTAR * (pitchFiltradoDeg + gy_dps * dt) +
    (1.0 - ALPHA_COMPLEMENTAR) * pitchAccDeg;

  aceleracaoResultanteG = sqrt(ax_g * ax_g + ay_g * ay_g + az_g * az_g);
  vibracaoInstantaneaG = fabs(aceleracaoResultanteG - 1.0);
}

void processarVibracao() {
  somaQuadradosVibracao += vibracaoInstantaneaG * vibracaoInstantaneaG;

  if (vibracaoInstantaneaG > picoVibracaoG) {
    picoVibracaoG = vibracaoInstantaneaG;
  }

  contadorVibracao++;

  if (contadorVibracao >= TAM_JANELA_VIBRACAO) {
    vibracaoRmsG = sqrt(somaQuadradosVibracao / contadorVibracao);
    vibracaoPicoG = picoVibracaoG;

    somaQuadradosVibracao = 0.0;
    picoVibracaoG = 0.0;
    contadorVibracao = 0;

    novaJanelaVibracaoDisponivel = true;
  }
}

// =======================================================
// ALARMES
// =======================================================

void atualizarAlarmeDistancia() {
  if (isnan(distanciaFiltradaCm)) {
    contadorAlarmeDistanciaInvalida++;

    if (contadorAlarmeDistanciaInvalida >= CONTAGEM_CONFIRMACAO_ALARME) {
      estadoDistancia = DISTANCIA_INVALIDA;
    }

    return;
  }

  contadorAlarmeDistanciaInvalida = 0;

  if (estadoDistancia == DISTANCIA_BAIXA) {
    if (distanciaFiltradaCm > LIMITE_DISTANCIA_BAIXA_CM + HISTERESE_DISTANCIA_CM) {
      estadoDistancia = DISTANCIA_NORMAL;
      contadorAlarmeDistanciaBaixa = 0;
    }
    return;
  }

  if (estadoDistancia == DISTANCIA_ALTA) {
    if (distanciaFiltradaCm < LIMITE_DISTANCIA_ALTA_CM - HISTERESE_DISTANCIA_CM) {
      estadoDistancia = DISTANCIA_NORMAL;
      contadorAlarmeDistanciaAlta = 0;
    }
    return;
  }

  if (distanciaFiltradaCm < LIMITE_DISTANCIA_BAIXA_CM) {
    contadorAlarmeDistanciaBaixa++;
    contadorAlarmeDistanciaAlta = 0;

    if (contadorAlarmeDistanciaBaixa >= CONTAGEM_CONFIRMACAO_ALARME) {
      estadoDistancia = DISTANCIA_BAIXA;
    }
  } else if (distanciaFiltradaCm > LIMITE_DISTANCIA_ALTA_CM) {
    contadorAlarmeDistanciaAlta++;
    contadorAlarmeDistanciaBaixa = 0;

    if (contadorAlarmeDistanciaAlta >= CONTAGEM_CONFIRMACAO_ALARME) {
      estadoDistancia = DISTANCIA_ALTA;
    }
  } else {
    contadorAlarmeDistanciaBaixa = 0;
    contadorAlarmeDistanciaAlta = 0;
    estadoDistancia = DISTANCIA_NORMAL;
  }
}

void atualizarAlarmeInclinacao() {
  float inclinacaoMaxAbs = fmax(fabs(pitchFiltradoDeg), fabs(rollFiltradoDeg));

  if (estadoInclinacao == ALARME_CRITICO) {
    if (inclinacaoMaxAbs < LIMITE_INCLINACAO_CRITICA_DEG - HISTERESE_INCLINACAO_DEG) {
      estadoInclinacao = ALARME_ALERTA;
    }
    return;
  }

  if (estadoInclinacao == ALARME_ALERTA) {
    if (inclinacaoMaxAbs >= LIMITE_INCLINACAO_CRITICA_DEG) {
      contadorAlarmeInclinacaoCritica++;

      if (contadorAlarmeInclinacaoCritica >= CONTAGEM_CONFIRMACAO_ALARME) {
        estadoInclinacao = ALARME_CRITICO;
      }
    } else if (inclinacaoMaxAbs < LIMITE_INCLINACAO_ALERTA_DEG - HISTERESE_INCLINACAO_DEG) {
      estadoInclinacao = ALARME_NORMAL;
      contadorAlarmeInclinacaoAlerta = 0;
      contadorAlarmeInclinacaoCritica = 0;
    }

    return;
  }

  if (inclinacaoMaxAbs >= LIMITE_INCLINACAO_CRITICA_DEG) {
    contadorAlarmeInclinacaoCritica++;
    contadorAlarmeInclinacaoAlerta = 0;

    if (contadorAlarmeInclinacaoCritica >= CONTAGEM_CONFIRMACAO_ALARME) {
      estadoInclinacao = ALARME_CRITICO;
    }
  } else if (inclinacaoMaxAbs >= LIMITE_INCLINACAO_ALERTA_DEG) {
    contadorAlarmeInclinacaoAlerta++;
    contadorAlarmeInclinacaoCritica = 0;

    if (contadorAlarmeInclinacaoAlerta >= CONTAGEM_CONFIRMACAO_ALARME) {
      estadoInclinacao = ALARME_ALERTA;
    }
  } else {
    estadoInclinacao = ALARME_NORMAL;
    contadorAlarmeInclinacaoAlerta = 0;
    contadorAlarmeInclinacaoCritica = 0;
  }
}

void atualizarAlarmeVibracao() {
  if (!novaJanelaVibracaoDisponivel) return;

  novaJanelaVibracaoDisponivel = false;

  if (estadoVibracao == ALARME_CRITICO) {
    if (vibracaoRmsG < LIMITE_VIBRACAO_CRITICA_G - HISTERESE_VIBRACAO_G) {
      estadoVibracao = ALARME_ALERTA;
    }
    return;
  }

  if (estadoVibracao == ALARME_ALERTA) {
    if (vibracaoRmsG >= LIMITE_VIBRACAO_CRITICA_G) {
      contadorAlarmeVibracaoCritica++;

      if (contadorAlarmeVibracaoCritica >= CONTAGEM_CONFIRMACAO_ALARME) {
        estadoVibracao = ALARME_CRITICO;
      }
    } else if (vibracaoRmsG < LIMITE_VIBRACAO_ALERTA_G - HISTERESE_VIBRACAO_G) {
      estadoVibracao = ALARME_NORMAL;
      contadorAlarmeVibracaoAlerta = 0;
      contadorAlarmeVibracaoCritica = 0;
    }

    return;
  }

  if (vibracaoRmsG >= LIMITE_VIBRACAO_CRITICA_G) {
    contadorAlarmeVibracaoCritica++;
    contadorAlarmeVibracaoAlerta = 0;

    if (contadorAlarmeVibracaoCritica >= CONTAGEM_CONFIRMACAO_ALARME) {
      estadoVibracao = ALARME_CRITICO;
    }
  } else if (vibracaoRmsG >= LIMITE_VIBRACAO_ALERTA_G) {
    contadorAlarmeVibracaoAlerta++;
    contadorAlarmeVibracaoCritica = 0;

    if (contadorAlarmeVibracaoAlerta >= CONTAGEM_CONFIRMACAO_ALARME) {
      estadoVibracao = ALARME_ALERTA;
    }
  } else {
    estadoVibracao = ALARME_NORMAL;
    contadorAlarmeVibracaoAlerta = 0;
    contadorAlarmeVibracaoCritica = 0;
  }
}

void atualizarLedAlarme() {
  bool existeAlarme =
    estadoDistancia != DISTANCIA_NORMAL ||
    estadoInclinacao != ALARME_NORMAL ||
    estadoVibracao != ALARME_NORMAL;

  digitalWrite(PINO_LED_ALARME, existeAlarme ? HIGH : LOW);
}

// =======================================================
// EVENTOS POR MUDANÇA DE ESTADO
// =======================================================

void inicializarEstadosEnviados() {
  ultimoEstadoDistanciaEnviado = textoEstadoDistancia(estadoDistancia);
  ultimoEstadoInclinacaoEnviado = textoEstadoNivel(estadoInclinacao);
  ultimoEstadoVibracaoEnviado = textoEstadoNivel(estadoVibracao);
}

String montarEventosMudancaEstado() {
  String eventos = "[";
  bool primeiro = true;

  String atualDist = textoEstadoDistancia(estadoDistancia);
  String atualInc = textoEstadoNivel(estadoInclinacao);
  String atualVib = textoEstadoNivel(estadoVibracao);

  if (ultimoEstadoDistanciaEnviado == "") ultimoEstadoDistanciaEnviado = atualDist;
  if (ultimoEstadoInclinacaoEnviado == "") ultimoEstadoInclinacaoEnviado = atualInc;
  if (ultimoEstadoVibracaoEnviado == "") ultimoEstadoVibracaoEnviado = atualVib;

  if (atualDist != ultimoEstadoDistanciaEnviado) {
    if (!primeiro) eventos += ",";
    eventos += "{\"tipo\":\"ALARME_DISTANCIA\",\"estado_anterior\":";
    eventos += jsonString(ultimoEstadoDistanciaEnviado);
    eventos += ",\"estado_atual\":";
    eventos += jsonString(atualDist);
    eventos += ",\"valor\":";
    eventos += jsonFloat(distanciaFiltradaCm, 2);
    eventos += "}";
    primeiro = false;
  }

  if (atualInc != ultimoEstadoInclinacaoEnviado) {
    if (!primeiro) eventos += ",";
    eventos += "{\"tipo\":\"ALARME_INCLINACAO\",\"estado_anterior\":";
    eventos += jsonString(ultimoEstadoInclinacaoEnviado);
    eventos += ",\"estado_atual\":";
    eventos += jsonString(atualInc);
    eventos += ",\"valor\":";
    eventos += jsonFloat(fmax(fabs(pitchFiltradoDeg), fabs(rollFiltradoDeg)), 2);
    eventos += "}";
    primeiro = false;
  }

  if (atualVib != ultimoEstadoVibracaoEnviado) {
    if (!primeiro) eventos += ",";
    eventos += "{\"tipo\":\"ALARME_VIBRACAO\",\"estado_anterior\":";
    eventos += jsonString(ultimoEstadoVibracaoEnviado);
    eventos += ",\"estado_atual\":";
    eventos += jsonString(atualVib);
    eventos += ",\"valor\":";
    eventos += jsonFloat(vibracaoRmsG, 5);
    eventos += "}";
    primeiro = false;
  }

  eventos += "]";
  return eventos;
}

void confirmarEstadosEnviados() {
  ultimoEstadoDistanciaEnviado = textoEstadoDistancia(estadoDistancia);
  ultimoEstadoInclinacaoEnviado = textoEstadoNivel(estadoInclinacao);
  ultimoEstadoVibracaoEnviado = textoEstadoNivel(estadoVibracao);
}

// =======================================================
// GOOGLE SHEETS
// =======================================================

String montarPayloadGoogleSheets() {
  String json = "{";

  json += "\"token\":";
  json += jsonString(TOKEN_API);
  json += ",";

  json += "\"device_id\":";
  json += jsonString(DEVICE_ID);
  json += ",";

  json += "\"millis_esp32\":";
  json += String(millis());
  json += ",";

  json += "\"distancia_cm\":";
  json += jsonFloat(distanciaFiltradaCm, 2);
  json += ",";

  json += "\"pitch_deg\":";
  json += jsonFloat(pitchFiltradoDeg, 3);
  json += ",";

  json += "\"roll_deg\":";
  json += jsonFloat(rollFiltradoDeg, 3);
  json += ",";

  json += "\"aceleracao_resultante_g\":";
  json += jsonFloat(aceleracaoResultanteG, 4);
  json += ",";

  json += "\"vibracao_rms_g\":";
  json += jsonFloat(vibracaoRmsG, 5);
  json += ",";

  json += "\"vibracao_pico_g\":";
  json += jsonFloat(vibracaoPicoG, 5);
  json += ",";

  json += "\"alarme_distancia\":";
  json += jsonString(textoEstadoDistancia(estadoDistancia));
  json += ",";

  json += "\"alarme_inclinacao\":";
  json += jsonString(textoEstadoNivel(estadoInclinacao));
  json += ",";

  json += "\"alarme_vibracao\":";
  json += jsonString(textoEstadoNivel(estadoVibracao));
  json += ",";

  json += "\"rssi_wifi\":";
  json += String(WiFi.RSSI());
  json += ",";

  json += "\"eventos\":";
  json += montarEventosMudancaEstado();

  json += "}";

  return json;
}

void enviarDadosGoogleSheets() {
  if (!wifiConectadoComoStation) {
    ultimoEnvioGoogleOk = false;
    ultimoHttpCodeGoogle = 0;
    ultimoErroGoogle = "Modo AP ativo. Sem envio ao Google.";
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    ultimoEnvioGoogleOk = false;
    ultimoHttpCodeGoogle = 0;
    ultimoErroGoogle = "Wi-Fi desconectado.";
    return;
  }

  String payload = montarPayloadGoogleSheets();

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(4000);

  if (!http.begin(client, GOOGLE_SCRIPT_URL)) {
    ultimoEnvioGoogleOk = false;
    ultimoHttpCodeGoogle = 0;
    ultimoErroGoogle = "Falha em http.begin.";
    return;
  }

  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST(payload);
  ultimoHttpCodeGoogle = httpCode;
  ultimoEnvioGoogleMillis = millis();

  if (httpCode > 0) {
    String resposta = http.getString();

    if (httpCode == 200 && resposta.indexOf("\"status\":\"ok\"") >= 0) {
      ultimoEnvioGoogleOk = true;
      ultimoErroGoogle = "Envio realizado com sucesso.";
      confirmarEstadosEnviados();
    } else {
      ultimoEnvioGoogleOk = false;
      ultimoErroGoogle = "Resposta inesperada: " + resposta;
    }
  } else {
    ultimoEnvioGoogleOk = false;
    ultimoErroGoogle = http.errorToString(httpCode);
  }

  http.end();
}

void buscarConfiguracaoGoogleSheets() {
  if (!wifiConectadoComoStation || WiFi.status() != WL_CONNECTED) {
    ultimaConfigGoogleOk = false;
    ultimoErroConfigGoogle = "Sem Wi-Fi para buscar configuracao.";
    return;
  }

  String url = String(GOOGLE_SCRIPT_URL) + "?action=config&token=" + TOKEN_API;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(4000);

  if (!http.begin(client, url)) {
    ultimaConfigGoogleOk = false;
    ultimoErroConfigGoogle = "Falha em http.begin config.";
    return;
  }

  int httpCode = http.GET();

  if (httpCode == 200) {
    String resposta = http.getString();

    if (resposta.indexOf("\"status\":\"ok\"") >= 0) {
      LIMITE_DISTANCIA_BAIXA_CM = extrairFloatJson(resposta, "limite_distancia_baixa_cm", LIMITE_DISTANCIA_BAIXA_CM);
      LIMITE_DISTANCIA_ALTA_CM = extrairFloatJson(resposta, "limite_distancia_alta_cm", LIMITE_DISTANCIA_ALTA_CM);
      HISTERESE_DISTANCIA_CM = extrairFloatJson(resposta, "histerese_distancia_cm", HISTERESE_DISTANCIA_CM);

      LIMITE_INCLINACAO_ALERTA_DEG = extrairFloatJson(resposta, "limite_inclinacao_alerta_deg", LIMITE_INCLINACAO_ALERTA_DEG);
      LIMITE_INCLINACAO_CRITICA_DEG = extrairFloatJson(resposta, "limite_inclinacao_critica_deg", LIMITE_INCLINACAO_CRITICA_DEG);
      HISTERESE_INCLINACAO_DEG = extrairFloatJson(resposta, "histerese_inclinacao_deg", HISTERESE_INCLINACAO_DEG);

      LIMITE_VIBRACAO_ALERTA_G = extrairFloatJson(resposta, "limite_vibracao_alerta_g", LIMITE_VIBRACAO_ALERTA_G);
      LIMITE_VIBRACAO_CRITICA_G = extrairFloatJson(resposta, "limite_vibracao_critica_g", LIMITE_VIBRACAO_CRITICA_G);
      HISTERESE_VIBRACAO_G = extrairFloatJson(resposta, "histerese_vibracao_g", HISTERESE_VIBRACAO_G);

      ALPHA_COMPLEMENTAR = extrairFloatJson(resposta, "alpha_complementar", ALPHA_COMPLEMENTAR);
      CONTAGEM_CONFIRMACAO_ALARME = extrairIntJson(resposta, "contagem_confirmacao_alarme", CONTAGEM_CONFIRMACAO_ALARME);

      ultimaConfigGoogleOk = true;
      ultimoErroConfigGoogle = "Configuracao atualizada.";
      ultimaConfigGoogleMillis = millis();
    } else {
      ultimaConfigGoogleOk = false;
      ultimoErroConfigGoogle = "Resposta config sem status ok.";
    }
  } else {
    ultimaConfigGoogleOk = false;
    ultimoErroConfigGoogle = "HTTP config: " + String(httpCode);
  }

  http.end();
}

// =======================================================
// SERVIDOR LOCAL SIMPLES
// =======================================================

String montarStatusJson() {
  unsigned long segundosDesdeUltimoEnvio =
    ultimoEnvioGoogleMillis == 0 ? 0 : (millis() - ultimoEnvioGoogleMillis) / 1000;

  String json = "{";

  json += "\"device_id\":";
  json += jsonString(DEVICE_ID);
  json += ",";

  json += "\"millis\":";
  json += String(millis());
  json += ",";

  json += "\"wifi_conectado\":";
  json += (WiFi.status() == WL_CONNECTED ? "true" : "false");
  json += ",";

  json += "\"modo_station\":";
  json += (wifiConectadoComoStation ? "true" : "false");
  json += ",";

  json += "\"ip\":";
  json += jsonString(WiFi.localIP().toString());
  json += ",";

  json += "\"rssi\":";
  json += String(WiFi.RSSI());
  json += ",";

  json += "\"distancia_cm\":";
  json += jsonFloat(distanciaFiltradaCm, 2);
  json += ",";

  json += "\"pitch_deg\":";
  json += jsonFloat(pitchFiltradoDeg, 3);
  json += ",";

  json += "\"roll_deg\":";
  json += jsonFloat(rollFiltradoDeg, 3);
  json += ",";

  json += "\"vibracao_rms_g\":";
  json += jsonFloat(vibracaoRmsG, 5);
  json += ",";

  json += "\"vibracao_pico_g\":";
  json += jsonFloat(vibracaoPicoG, 5);
  json += ",";

  json += "\"alarme_distancia\":";
  json += jsonString(textoEstadoDistancia(estadoDistancia));
  json += ",";

  json += "\"alarme_inclinacao\":";
  json += jsonString(textoEstadoNivel(estadoInclinacao));
  json += ",";

  json += "\"alarme_vibracao\":";
  json += jsonString(textoEstadoNivel(estadoVibracao));
  json += ",";

  json += "\"google_ok\":";
  json += ultimoEnvioGoogleOk ? "true" : "false";
  json += ",";

  json += "\"google_http_code\":";
  json += String(ultimoHttpCodeGoogle);
  json += ",";

  json += "\"google_ultimo_envio_s\":";
  json += String(segundosDesdeUltimoEnvio);
  json += ",";

  json += "\"google_msg\":";
  json += jsonString(ultimoErroGoogle);
  json += ",";

  json += "\"config_ok\":";
  json += ultimaConfigGoogleOk ? "true" : "false";
  json += ",";

  json += "\"config_msg\":";
  json += jsonString(ultimoErroConfigGoogle);

  json += "}";

  return json;
}

void handleStatus() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", montarStatusJson());
}

void handleRoot() {
  String html = "";
  html += "<!DOCTYPE html><html lang='pt-BR'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Status ESP32 Pulverizador</title>";
  html += "<style>";
  html += "body{font-family:Arial;background:#0f172a;color:#e5e7eb;padding:20px}";
  html += ".card{background:#111827;border:1px solid #374151;border-radius:14px;padding:16px;max-width:720px;margin:auto}";
  html += "pre{white-space:pre-wrap;background:#020617;padding:12px;border-radius:10px}";
  html += ".ok{color:#22c55e}.falha{color:#ef4444}";
  html += "</style></head><body>";
  html += "<div class='card'>";
  html += "<h1>Status local - ESP32 Pulverizador</h1>";
  html += "<p>Diagnóstico simples em campo. Endpoint JSON: <b>/status</b></p>";
  html += "<pre id='status'>Carregando...</pre>";
  html += "</div>";
  html += "<script>";
  html += "async function atualizar(){";
  html += "try{const r=await fetch('/status',{cache:'no-store'});";
  html += "const d=await r.json();";
  html += "document.getElementById('status').textContent=JSON.stringify(d,null,2);";
  html += "}catch(e){document.getElementById('status').textContent='Falha ao ler /status: '+e;}";
  html += "}";
  html += "setInterval(atualizar,2000);atualizar();";
  html += "</script></body></html>";

  server.send(200, "text/html", html);
}

void handleNotFound() {
  server.send(404, "text/plain", "Recurso nao encontrado");
}

void configurarServidorWeb() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("Servidor local simples iniciado.");
}

// =======================================================
// TAREFA GOOGLE EM CORE SEPARADO
// =======================================================

void tarefaGoogle(void* parametro) {
  unsigned long ultimoEnvio = 0;
  unsigned long ultimaConfig = 0;

  while (true) {
    unsigned long agora = millis();

    if (wifiConectadoComoStation && WiFi.status() == WL_CONNECTED) {
      if (agora - ultimaConfig >= INTERVALO_CONFIG_GOOGLE_MS) {
        ultimaConfig = agora;
        buscarConfiguracaoGoogleSheets();
      }

      if (agora - ultimoEnvio >= INTERVALO_ENVIO_GOOGLE_MS) {
        ultimoEnvio = agora;
        enviarDadosGoogleSheets();
      }
    }

    vTaskDelay(250 / portTICK_PERIOD_MS);
  }
}

// =======================================================
// WI-FI
// =======================================================

void conectarWiFi() {
  Serial.println();
  Serial.print("Conectando ao Wi-Fi: ");
  Serial.println(WIFI_SSID);

  wifiConectadoComoStation = false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long inicio = millis();
  const unsigned long TIMEOUT_WIFI_MS = 15000;

  while (WiFi.status() != WL_CONNECTED && millis() - inicio < TIMEOUT_WIFI_MS) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiConectadoComoStation = true;

    Serial.println("Wi-Fi conectado.");
    Serial.print("IP do ESP32: ");
    Serial.println(WiFi.localIP());
    Serial.print("Status local: http://");
    Serial.print(WiFi.localIP());
    Serial.println("/status");
  } else {
    wifiConectadoComoStation = false;

    Serial.println("Falha no Wi-Fi. Iniciando modo AP.");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);

    IPAddress ipAP = WiFi.softAPIP();

    Serial.print("Rede AP: ");
    Serial.println(AP_SSID);
    Serial.print("Senha: ");
    Serial.println(AP_PASSWORD);
    Serial.print("Status local AP: http://");
    Serial.print(ipAP);
    Serial.println("/status");
  }

  Serial.println();
}

// =======================================================
// SERIAL
// =======================================================

void imprimirSerialMonitor() {
  Serial.print("dist_cm: ");
  Serial.print(isnan(distanciaFiltradaCm) ? -1 : distanciaFiltradaCm, 2);

  Serial.print(" | pitch: ");
  Serial.print(pitchFiltradoDeg, 2);

  Serial.print(" | roll: ");
  Serial.print(rollFiltradoDeg, 2);

  Serial.print(" | vib_rms: ");
  Serial.print(vibracaoRmsG, 4);

  Serial.print(" | alarmes: ");
  Serial.print(textoEstadoDistancia(estadoDistancia));
  Serial.print(" / ");
  Serial.print(textoEstadoNivel(estadoInclinacao));
  Serial.print(" / ");
  Serial.print(textoEstadoNivel(estadoVibracao));

  Serial.print(" | google: ");
  Serial.println(ultimoEnvioGoogleOk ? "OK" : "FALHA");
}

// =======================================================
// SETUP
// =======================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==============================================");
  Serial.println("Projeto IoT Pulverizador - Firmware leve");
  Serial.println("ESP32 + Google Sheets + /status local");
  Serial.println("==============================================");

  pinMode(PINO_TRIG, OUTPUT);
  pinMode(PINO_ECHO, INPUT);
  pinMode(PINO_LED_ALARME, OUTPUT);

  digitalWrite(PINO_TRIG, LOW);
  digitalWrite(PINO_LED_ALARME, LOW);

  Wire.begin(PINO_SDA, PINO_SCL);

  if (!mpu.begin()) {
    Serial.println("ERRO: MPU-6050 nao encontrado.");
    while (1) delay(1000);
  }

  Serial.println("MPU-6050 encontrado.");

  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  for (int i = 0; i < TAM_FILTRO_DISTANCIA; i++) {
    bufferDistancia[i] = 0.0;
  }

  calibrarMPU6050(300);

  inicializarEstadosEnviados();

  conectarWiFi();
  configurarServidorWeb();

  xTaskCreatePinnedToCore(
    tarefaGoogle,
    "TarefaGoogle",
    10000,
    NULL,
    1,
    NULL,
    0
  );

  tempoAnteriorMPU = millis();
  tempoAnteriorDistancia = millis();
  tempoAnteriorImpressao = millis();

  Serial.println("Sistema iniciado.");
}

// =======================================================
// LOOP
// =======================================================

void loop() {
  unsigned long tempoAtual = millis();

  server.handleClient();

  if (tempoAtual - tempoAnteriorMPU >= INTERVALO_MPU_MS) {
    float dt = (tempoAtual - tempoAnteriorMPU) / 1000.0;
    tempoAnteriorMPU = tempoAtual;

    processarMPU6050(dt);
    processarVibracao();

    atualizarAlarmeInclinacao();
    atualizarAlarmeVibracao();
    atualizarLedAlarme();
  }

  if (tempoAtual - tempoAnteriorDistancia >= INTERVALO_DISTANCIA_MS) {
    tempoAnteriorDistancia = tempoAtual;

    distanciaCm = lerDistanciaCm();
    distanciaFiltradaCm = filtrarDistancia(distanciaCm);

    atualizarAlarmeDistancia();
    atualizarLedAlarme();
  }

  if (tempoAtual - tempoAnteriorImpressao >= INTERVALO_IMPRESSAO_MS) {
    tempoAnteriorImpressao = tempoAtual;
    imprimirSerialMonitor();
  }
}
