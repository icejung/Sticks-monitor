// ---------------------------------------------------------------------------
//  StickS3 미니 모니터
//
//    Page 1 : 날씨 + 짧은 메모          ← GitHub status.json (5분마다 갱신)
//    Page 2 : 시세 (삼성전자 / XRP / KODEX) ← GitHub status.json
//    Page 3 : Gemini 무료 등급 음성 Q&A  ← 마이크 녹음 → WAV → generateContent
//
//  하드웨어: M5Stack StickS3 (ESP32-S3-PICO-1-N8R8, 8MB Flash + 8MB OPI PSRAM)
//            1.14" ST7789P3 135x240 LCD, ES8311 코덱 + MEMS 마이크
//            버튼 A = GPIO11, 버튼 B = GPIO12, 250mAh 배터리(M5PM1 PMIC)
//
//  버튼
//    A (짧게) : 다음 페이지
//    B (짧게) : 1·2페이지 = status.json 새로고침 / 3페이지 = 녹음 시작·중지
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>

#include <string>
#include <vector>

// textdatum_t / top_left / top_right / middle_center 등을 바로 쓰기 위해
using namespace lgfx::textdatum;

#if __has_include("config.h")
  #include "config.h"
#else
  #error "include/config.example.h -> include/config.h 로 복사한 뒤 Wi-Fi / Gemini 키를 채우세요."
#endif

// ----- 설정 기본값 (config.h 에서 덮어쓸 수 있음) -----
#ifndef STATUS_URL
  #define STATUS_URL "https://raw.githubusercontent.com/icejung/sticks-monitor/main/status.json"
#endif
#ifndef STATUS_REFRESH_MS
  #define STATUS_REFRESH_MS (5UL * 60UL * 1000UL)
#endif
#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 1
#endif
#ifndef DISPLAY_BRIGHTNESS
  #define DISPLAY_BRIGHTNESS 140
#endif
#ifndef GEMINI_ENABLED
  #define GEMINI_ENABLED 1
#endif
#ifndef GEMINI_API_KEY
  #define GEMINI_API_KEY ""
#endif
#ifndef GEMINI_MODEL
  #define GEMINI_MODEL "gemini-3.5-flash-lite"
#endif
#ifndef GEMINI_RECORD_SEC
  #define GEMINI_RECORD_SEC 5
#endif
#ifndef GEMINI_MIC_WARMUP_MS
  #define GEMINI_MIC_WARMUP_MS 1200
#endif
#ifndef GEMINI_PROMPT
  #define GEMINI_PROMPT \
    "첨부한 한국어 음성 질문을 듣고 한국어로 답해라. 2~3줄, 60자 이내, 평문으로 핵심만."
#endif

// ---------------------------------------------------------------------------
//  화면 기본값
// ---------------------------------------------------------------------------
static constexpr int W = 240;   // 가로
static constexpr int H = 135;   // 세로
static constexpr int HEADER_H = 20;
static constexpr int FOOTER_H = 16;
static constexpr int BODY_TOP = HEADER_H + 2;
static constexpr int BODY_BOTTOM = H - FOOTER_H - 2;

// 색 (RGB565)
static constexpr uint16_t C_BG      = 0x0000;
static constexpr uint16_t C_PANEL   = 0x1082;   // 아주 어두운 회색
static constexpr uint16_t C_FG      = 0xFFFF;
static constexpr uint16_t C_DIM     = 0x8C51;
static constexpr uint16_t C_ACCENT  = 0x07FF;   // 시안
static constexpr uint16_t C_UP      = 0xFBE0;   // 상승 = 빨강(국내 관행)
static constexpr uint16_t C_DOWN    = 0x4D7F;   // 하락 = 파랑
static constexpr uint16_t C_OK      = 0x07E0;
static constexpr uint16_t C_WARN    = 0xFE60;
static constexpr uint16_t C_BAD     = 0xF800;

// ---------------------------------------------------------------------------
//  전역 상태
// ---------------------------------------------------------------------------
struct MarketRow {
  String name;
  String price;     // 이미 포맷된 문자열
  String change;    // "+1.23%" 형태
  int    dir = 0;   // +1 상승 / -1 하락 / 0 보합
};

struct MonitorData {
  bool   valid = false;
  String updated;    // "15:49" 같은 표시용 문자열
  String note;
  String city;
  String condition;
  String air;
  float  temp = 0, high = 0, low = 0;
  int    humidity = 0;
  MarketRow markets[4];
  int    marketCount = 0;
  String error;      // 마지막 실패 사유
};

static MonitorData g_data;
static uint32_t    g_nextFetchAt = 0;
static String      g_bootMsg;

static int  g_page = 0;
static bool g_needDraw = true;
static uint32_t g_lastDrawAt = 0;

// Gemini 상태
enum GemState : uint8_t { G_IDLE, G_WARMUP, G_REC, G_SEND, G_ANSWER, G_ERROR };
struct Gemini {
  GemState state = G_IDLE;
  uint32_t t0 = 0;
  size_t   samples = 0;
  String   text;
  String   err;
  int      scroll = 0;
  uint32_t scrollAt = 0;
};
static Gemini g_gem;

static constexpr size_t   GEM_SR = 16000;
static int16_t* g_micBuf = nullptr;   // PSRAM: WAV 헤더(44) + PCM
static char*    g_body   = nullptr;   // PSRAM: HTTPS 요청 본문
static size_t   g_bodyCap = 0;

static lgfx::LGFX_Sprite  g_spr(&M5.Display);
static lgfx::LGFXBase*    g_ui = &M5.Display;
static bool               g_spriteOk = false;

// ---------------------------------------------------------------------------
//  작은 도우미
// ---------------------------------------------------------------------------
static void uiBegin() { g_ui->startWrite(); }
static void uiEnd() {
  g_ui->endWrite();
  if (g_spriteOk) { g_spr.pushSprite(0, 0); }
}

static void putText(const char* s, int x, int y, uint16_t fg, const lgfx::IFont* font,
                    textdatum_t datum = top_left) {
  g_ui->setFont(font);
  g_ui->setTextDatum(datum);
  g_ui->setTextColor(fg);          // 배경 투명 (먼저 fillScreen 으로 칠한 뒤 그림)
  g_ui->drawString(s, x, y);
}

// UTF-8 안전 줄바꿈 (현재 폰트 기준 실제 픽셀 폭으로 계산)
static void wrapText(const String& text, int maxW, std::vector<String>& out) {
  out.clear();
  String line;
  size_t i = 0;
  while (i < text.length()) {
    unsigned char c = (unsigned char)text[i];
    size_t len = 1;
    if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;
    if (i + len > text.length()) len = 1;
    String ch = text.substring(i, i + len);
    i += len;

    if (ch == "\r") continue;
    if (ch == "\n") { out.push_back(line); line = ""; continue; }

    String test = line + ch;
    if (line.length() > 0 && g_ui->textWidth(test.c_str()) > maxW) {
      out.push_back(line);
      line = ch;
    } else {
      line = test;
    }
  }
  out.push_back(line);
}

static String fmtThousands(long v) {
  char raw[16];
  snprintf(raw, sizeof(raw), "%ld", v < 0 ? -v : v);
  String s(raw);
  String out;
  int n = s.length();
  for (int i = 0; i < n; ++i) {
    if (i > 0 && (n - i) % 3 == 0) out += ',';
    out += s[i];
  }
  return (v < 0 ? "-" : "") + out;
}

static String fmtNumber(float v, int decimals) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%.*f", decimals, (double)v);
  return String(buf);
}

static bool sameText(const char* a, const char* b) { return a && b && strcmp(a, b) == 0; }

// ---------------------------------------------------------------------------
//  배터리
// ---------------------------------------------------------------------------
static int batteryLevel() {
  int lv = M5.Power.getBatteryLevel();
  return (lv < 0) ? -1 : lv;      // -1 = 읽기 실패
}

static void drawBatteryIcon(int x, int y, int lv, bool charging) {
  const int w = 20, h = 10;
  g_ui->drawRect(x, y, w, h, C_DIM);
  g_ui->fillRect(x + w, y + 3, 2, 4, C_DIM);
  if (lv >= 0) {
    int fill = (w - 4) * lv / 100;
    uint16_t c = (lv > 50) ? C_OK : (lv > 20 ? C_WARN : C_BAD);
    g_ui->fillRect(x + 2, y + 2, fill, h - 4, c);
  }
  if (charging) {
    g_ui->fillRect(x + 8, y + 1, 3, 5, C_ACCENT);
    g_ui->fillRect(x + 6, y + 5, 3, 4, C_ACCENT);
  }
}

// ---------------------------------------------------------------------------
//  화면 그리기
// ---------------------------------------------------------------------------
static const char* pageTitle(int p) {
  switch (p) {
    case 0: return "날씨";
    case 1: return "시세";
    default: return "Gemini";
  }
}

static void drawHeader() {
  g_ui->fillRect(0, 0, W, HEADER_H, C_PANEL);
  putText(pageTitle(g_page), 4, 3, C_ACCENT, &fonts::efontKR_12);

  char pg[8];
  snprintf(pg, sizeof(pg), "%d/%d", g_page + 1, GEMINI_ENABLED ? 3 : 2);
  putText(pg, W - 56, 4, C_DIM, &fonts::efontKR_10, top_right);

  int lv = batteryLevel();
  if (lv >= 0) {
    char b[8];
    snprintf(b, sizeof(b), "%d%%", lv);
    putText(b, W - 30, 4, C_DIM, &fonts::efontKR_10, top_right);
  }
  drawBatteryIcon(W - 26, 5, lv, M5.Power.isCharging());
}

static void drawFooter(const char* hint) {
  g_ui->drawFastHLine(0, H - FOOTER_H, W, C_PANEL);
  putText(hint, 4, H - 13, C_DIM, &fonts::efontKR_10);
  if (g_data.valid && g_data.updated.length()) {
    String s = g_data.updated + " 기준";
    putText(s.c_str(), W - 4, H - 13, C_DIM, &fonts::efontKR_10, top_right);
  }
}

static void drawWeatherPage() {
  // 도시 + 현재 기온
  putText(g_data.city.length() ? g_data.city.c_str() : "—", 6, BODY_TOP + 2, C_FG,
          &fonts::efontKR_16);

  char temp[16];
  snprintf(temp, sizeof(temp), "%d°", (int)lroundf(g_data.temp));
  putText(temp, W - 6, BODY_TOP, C_ACCENT, &fonts::efontKR_24_b, top_right);

  // 날씨 상태 / 최고·최저
  String line = g_data.condition;
  putText(line.length() ? line.c_str() : "—", 6, BODY_TOP + 26, C_FG, &fonts::efontKR_12);

  char hl[40];
  snprintf(hl, sizeof(hl), "최고 %d°  최저 %d°", (int)lroundf(g_data.high), (int)lroundf(g_data.low));
  putText(hl, W - 6, BODY_TOP + 27, C_DIM, &fonts::efontKR_10, top_right);

  // 습도 / 미세먼지
  char env[56];
  if (g_data.air.length())
    snprintf(env, sizeof(env), "습도 %d%%  ·  미세먼지 %s", g_data.humidity, g_data.air.c_str());
  else
    snprintf(env, sizeof(env), "습도 %d%%", g_data.humidity);
  putText(env, 6, BODY_TOP + 43, C_DIM, &fonts::efontKR_12);

  // 짧은 메모
  g_ui->drawFastHLine(6, BODY_TOP + 59, W - 12, C_PANEL);
  g_ui->setFont(&fonts::efontKR_12);
  std::vector<String> lines;
  wrapText(g_data.note.length() ? g_data.note : "status.json 의 note 를 채워 주세요",
           W - 12, lines);
  int y = BODY_TOP + 63;
  for (size_t i = 0; i < lines.size() && y < BODY_BOTTOM - 10; ++i, y += 14) {
    putText(lines[i].c_str(), 6, y, C_FG, &fonts::efontKR_12);
  }

  if (!g_data.valid && g_data.error.length()) {
    putText(g_data.error.c_str(), 6, BODY_BOTTOM - 14, C_BAD, &fonts::efontKR_10);
  }
}

static void drawMarketPage() {
  if (g_data.marketCount == 0) {
    putText("시세 정보 없음", W / 2, H / 2, C_DIM, &fonts::efontKR_12, middle_center);
    return;
  }
  int n = g_data.marketCount;
  int avail = BODY_BOTTOM - BODY_TOP;
  int rowH = avail / n;
  if (rowH > 30) rowH = 30;
  int y = BODY_TOP + 2;

  for (int i = 0; i < n; ++i, y += rowH) {
    const MarketRow& m = g_data.markets[i];
    uint16_t col = (m.dir > 0) ? C_UP : (m.dir < 0 ? C_DOWN : C_DIM);

    if (i) g_ui->drawFastHLine(6, y - 2, W - 12, C_PANEL);

    // 이름
    putText(m.name.c_str(), 6, y + (rowH >= 28 ? 8 : 5), C_FG, &fonts::efontKR_12);

    // 가격 (오른쪽 정렬)
    putText(m.price.c_str(), W - 50, y + (rowH >= 28 ? 4 : 2), C_FG, &fonts::efontKR_16,
            top_right);

    // 등락률
    putText(m.change.c_str(), W - 6, y + (rowH >= 28 ? 8 : 5), col, &fonts::efontKR_12,
            top_right);
  }
}

static size_t maxSamplesNow() { return GEM_SR * (size_t)GEMINI_RECORD_SEC; }

static void drawGeminiPage() {
  const int x = 6;
  const int maxW = W - 12;

  switch (g_gem.state) {
    case G_IDLE: {
      if (!GEMINI_ENABLED) {
        putText("Gemini 페이지 꺼짐", W / 2, H / 2, C_DIM, &fonts::efontKR_12, middle_center);
        break;
      }
      bool noKey = (strlen(GEMINI_API_KEY) < 10) || sameText(GEMINI_API_KEY, "YOUR_GEMINI_API_KEY");
      if (noKey) {
        putText("GEMINI_API_KEY 필요", x, BODY_TOP + 8, C_BAD, &fonts::efontKR_12);
        putText("include/config.h 에", x, BODY_TOP + 28, C_DIM, &fonts::efontKR_12);
        putText("aistudio.google.com 키를 넣으세요", x, BODY_TOP + 42, C_DIM, &fonts::efontKR_10);
      } else {
        putText("B 를 눌러 말하기", W / 2, BODY_TOP + 14, C_FG, &fonts::efontKR_16, middle_center);
        char sub[48];
        snprintf(sub, sizeof(sub), "%d초 녹음 → Gemini", GEMINI_RECORD_SEC);
        putText(sub, W / 2, BODY_TOP + 40, C_DIM, &fonts::efontKR_12, middle_center);
        putText(GEMINI_MODEL, W / 2, BODY_TOP + 60, C_ACCENT, &fonts::efontKR_10, middle_center);
      }
      break;
    }

    case G_WARMUP:
      putText("마이크 준비 중…", W / 2, H / 2, C_DIM, &fonts::efontKR_12, middle_center);
      break;

    case G_REC: {
      float sec = g_gem.samples / (float)GEM_SR;
      char head[40];
      snprintf(head, sizeof(head), "듣는 중… %.1f / %d초", sec, GEMINI_RECORD_SEC);
      putText(head, x, BODY_TOP + 2, C_UP, &fonts::efontKR_12);

      // 진행 바
      int barW = maxW, barH = 10;
      int by = BODY_TOP + 24;
      g_ui->drawRect(x, by, barW, barH, C_DIM);
      int filled = (int)((maxSamplesNow() ? (float)g_gem.samples / maxSamplesNow() : 0) * (barW - 4));
      if (filled > 0) g_ui->fillRect(x + 2, by + 2, filled, barH - 4, C_ACCENT);

      putText("B: 그만", x, BODY_TOP + 44, C_DIM, &fonts::efontKR_10);
      break;
    }

    case G_SEND: {
      putText("생각 중…", W / 2, H / 2 - 8, C_ACCENT, &fonts::efontKR_16, middle_center);
      putText("Gemini 응답 기다리는 중", W / 2, H / 2 + 14, C_DIM, &fonts::efontKR_10,
              middle_center);
      break;
    }

    case G_ANSWER: {
      g_ui->setFont(&fonts::efontKR_12);
      std::vector<String> lines;
      wrapText(g_gem.text, maxW, lines);
      const int lineH = 14;
      int perPage = (BODY_BOTTOM - BODY_TOP) / lineH;      // 6줄 내외
      if (perPage < 1) perPage = 1;
      int pages = ((int)lines.size() + perPage - 1) / perPage;

      // 자동 스크롤
      if (pages > 1 && millis() - g_gem.scrollAt > 2500) {
        g_gem.scrollAt = millis();
        g_gem.scroll = (g_gem.scroll + 1) % pages;
        g_needDraw = true;
        return;
      }
      int start = g_gem.scroll * perPage;
      int y = BODY_TOP + 2;
      for (int i = start; i < (int)lines.size() && i < start + perPage; ++i, y += lineH) {
        putText(lines[i].c_str(), x, y, C_FG, &fonts::efontKR_12);
      }
      if (pages > 1) {
        char pg[12];
        snprintf(pg, sizeof(pg), "%d/%d", g_gem.scroll + 1, pages);
        putText(pg, W - 4, BODY_BOTTOM - 10, C_DIM, &fonts::efontKR_10, top_right);
      }
      break;
    }

    case G_ERROR: {
      g_ui->setFont(&fonts::efontKR_10);
      std::vector<String> lines;
      wrapText(g_gem.err, maxW, lines);
      int y = BODY_TOP + 4;
      for (size_t i = 0; i < lines.size() && y < BODY_BOTTOM - 8; ++i, y += 13) {
        putText(lines[i].c_str(), x, y, C_BAD, &fonts::efontKR_10);
      }
      putText("B: 다시 시도", x, BODY_BOTTOM - 12, C_DIM, &fonts::efontKR_10);
      break;
    }
  }
}

static void draw() {
  g_ui->startWrite();
  g_ui->fillScreen(C_BG);
  drawHeader();

  const char* hint = "";
  switch (g_page) {
    case 0:
      drawWeatherPage();
      hint = "A 시세    B 새로고침";
      break;
    case 1:
      drawMarketPage();
      hint = GEMINI_ENABLED ? "A Gemini    B 새로고침" : "A 날씨    B 새로고침";
      break;
    default:
      drawGeminiPage();
      hint = (g_gem.state == G_REC) ? "B 그만    A 다음" : "B 말하기    A 다음";
      break;
  }
  drawFooter(hint);
  g_ui->endWrite();
  if (g_spriteOk) { g_spr.pushSprite(0, 0); }

  g_lastDrawAt = millis();
  g_needDraw = false;
}

// ---------------------------------------------------------------------------
//  Wi-Fi
// ---------------------------------------------------------------------------
static void bootScreen(const char* msg) {
  g_ui->startWrite();
  g_ui->fillScreen(C_BG);
  putText("StickS3", 6, 8, C_ACCENT, &fonts::efontKR_12);
  putText(msg, 6, H / 2 - 8, C_FG, &fonts::efontKR_12);
  g_ui->endWrite();
  if (g_spriteOk) { g_spr.pushSprite(0, 0); }
}

static bool wifiConnect(uint32_t timeoutMs = 25000) {
  if (WiFi.status() == WL_CONNECTED) return true;

  bootScreen("Wi-Fi 연결 중…");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);          // TLS 중 지연/끊김 방지
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t t0 = millis();
  int dots = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(300);
    if (++dots % 4 == 0) {
      char b[32];
      snprintf(b, sizeof(b), "Wi-Fi 연결 중%.*s", dots % 16, "..........");
      putText(b, 6, H / 2 - 8, C_FG, &fonts::efontKR_12);
    }
  }
  if (WiFi.status() != WL_CONNECTED) {
    bootScreen("Wi-Fi 연결 실패");
    delay(800);
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
//  status.json 받아오기 (HTTPS, 토큰 불필요)
// ---------------------------------------------------------------------------
static String formatPrice(JsonVariant v, const char* currency) {
  float p = v | 0.0f;
  if (sameText(currency, "USD")) {
    return fmtNumber(p, p < 10 ? 4 : 2);
  }
  return fmtThousands((long)lroundf(p));
}

static bool parseStatus(const String& payload) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) return false;

  g_data.updated = String((const char*)(doc["updated"] | ""));
  g_data.note    = String((const char*)(doc["note"] | ""));

  JsonObject w = doc["weather"];
  if (!w.isNull()) {
    g_data.city      = String((const char*)(w["city"] | ""));
    g_data.condition = String((const char*)(w["condition"] | ""));
    g_data.air       = String((const char*)(w["air"] | ""));
    g_data.temp      = w["temp"] | 0.0f;
    g_data.high      = w["high"] | 0.0f;
    g_data.low       = w["low"]  | 0.0f;
    g_data.humidity  = w["humidity"] | 0;
  }

  g_data.marketCount = 0;
  JsonArray arr = doc["markets"];
  for (JsonObject m : arr) {
    if (g_data.marketCount >= 4) break;
    MarketRow& row = g_data.markets[g_data.marketCount++];
    row.name   = String((const char*)(m["name"] | "?"));
    row.price  = formatPrice(m["price"], (const char*)(m["currency"] | "KRW"));
    float chg  = m["change"] | 0.0f;
    char buf[24];
    snprintf(buf, sizeof(buf), "%+.2f%%", chg);
    row.change = String(buf);
    row.dir    = (chg > 0.001f) ? 1 : (chg < -0.001f ? -1 : 0);
  }

  g_data.valid = true;
  g_data.error = "";
  return true;
}

static bool fetchStatus() {
  if (!wifiConnect()) { g_data.error = "Wi-Fi 연결 실패"; g_data.valid = false; return false; }

  WiFiClientSecure client;
  client.setInsecure();             // 공개 파일 읽기: 인증서 검증 생략(시간 동기화 불필요)
  client.setHandshakeTimeout(10);

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(15000);
  http.setUserAgent("StickS3-Monitor/1.0");

  if (!http.begin(client, STATUS_URL)) {
    g_data.error = "요청 생성 실패";
    g_data.valid = false;
    return false;
  }

  bootScreen("status.json 받는 중…");
  int code = http.GET();
  if (code != 200) {
    char buf[40];
    snprintf(buf, sizeof(buf), "status.json HTTP %d", code);
    g_data.error = String(buf);
    g_data.valid = false;
    http.end();
    client.stop();
    return false;
  }

  String payload = http.getString();
  http.end();
  client.stop();

  if (!parseStatus(payload)) {
    g_data.error = "status.json 형식 오류";
    g_data.valid = false;
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
//  Gemini 무료 등급 음성 Q&A
// ---------------------------------------------------------------------------
static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t b64Encode(const uint8_t* src, size_t len, char* dst) {
  size_t o = 0, i = 0;
  for (; i + 2 < len; i += 3) {
    uint32_t v = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8) | src[i + 2];
    dst[o++] = B64[(v >> 18) & 63];
    dst[o++] = B64[(v >> 12) & 63];
    dst[o++] = B64[(v >> 6) & 63];
    dst[o++] = B64[v & 63];
  }
  size_t rest = len - i;
  if (rest == 1) {
    uint32_t v = (uint32_t)src[i] << 16;
    dst[o++] = B64[(v >> 18) & 63];
    dst[o++] = B64[(v >> 12) & 63];
    dst[o++] = '=';
    dst[o++] = '=';
  } else if (rest == 2) {
    uint32_t v = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8);
    dst[o++] = B64[(v >> 18) & 63];
    dst[o++] = B64[(v >> 12) & 63];
    dst[o++] = B64[(v >> 6) & 63];
    dst[o++] = '=';
  }
  dst[o] = '\0';
  return o;
}

static void w16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
static void w32(uint8_t* p, uint32_t v) {
  p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

// 44바이트 표준 WAV 헤더 (PCM / 모노 / 16bit)
static void writeWavHeader(uint8_t* p, uint32_t dataBytes, uint32_t rate) {
  memcpy(p, "RIFF", 4);
  w32(p + 4, 36 + dataBytes);
  memcpy(p + 8, "WAVEfmt ", 8);
  w32(p + 16, 16);            // fmt chunk size
  w16(p + 20, 1);             // PCM
  w16(p + 22, 1);             // mono
  w32(p + 24, rate);
  w32(p + 28, rate * 2);      // byte rate
  w16(p + 32, 2);             // block align
  w16(p + 34, 16);            // bits per sample
  memcpy(p + 36, "data", 4);
  w32(p + 40, dataBytes);
}

static bool gemAlloc() {
  if (g_micBuf && g_body) return true;

  const size_t maxSamples = maxSamplesNow();
  const size_t wavCap = 44 + maxSamples * 2;
  const size_t b64Cap = ((wavCap + 2) / 3) * 4 + 8;

  if (!g_micBuf) {
    g_micBuf = (int16_t*)heap_caps_malloc(wavCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_micBuf) g_micBuf = (int16_t*)heap_caps_malloc(wavCap, MALLOC_CAP_8BIT);
  }
  if (!g_body) {
    g_bodyCap = 512 + b64Cap + 128;
    g_body = (char*)heap_caps_malloc(g_bodyCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_body) g_body = (char*)heap_caps_malloc(g_bodyCap, MALLOC_CAP_8BIT);
  }
  return g_micBuf && g_body;
}

static void gemFail(const String& msg) {
  g_gem.state = G_ERROR;
  g_gem.err = msg;
  g_needDraw = true;
  draw();
}

static void gemStart() {
  if (!gemAlloc()) { gemFail("PSRAM 할당 실패"); return; }
  if (strlen(GEMINI_API_KEY) < 10 || sameText(GEMINI_API_KEY, "YOUR_GEMINI_API_KEY")) {
    gemFail("GEMINI_API_KEY 없음\ninclude/config.h 확인");
    return;
  }
  if (!wifiConnect()) { gemFail("Wi-Fi 연결 실패"); return; }

  // ES8311 코덱은 스피커와 마이크를 동시에 쓸 수 없다 → 스피커를 먼저 내린다.
  M5.Speaker.end();
  delay(30);

  if (!M5.Mic.begin() || !M5.Mic.isEnabled()) {
    gemFail("마이크 시작 실패");
    return;
  }
  g_gem.samples = 0;
  g_gem.state = G_WARMUP;
  g_gem.t0 = millis();
  g_needDraw = true;
}

static void gemStopRec() {
  // 남은 버퍼가 마이크 태스크에서 빠져나올 때까지 잠깐 대기
  uint32_t t0 = millis();
  while (M5.Mic.isRecording() && millis() - t0 < 3000) delay(10);
  M5.Mic.end();

  if (g_gem.samples < GEM_SR / 2) {      // 0.5초 미만
    gemFail("녹음이 너무 짧습니다");
    return;
  }
  g_gem.state = G_SEND;
  g_needDraw = true;
  draw();
  // (아래 gemSend 는 loop 에서 이어서 호출)
}

static void gemSend() {
  const size_t pcmBytes = g_gem.samples * 2;
  writeWavHeader((uint8_t*)g_micBuf, (uint32_t)pcmBytes, GEM_SR);
  const size_t wavLen = 44 + pcmBytes;

  const char* head =
      "{\"contents\":[{\"parts\":[{\"text\":\"" GEMINI_PROMPT "\"},"
      "{\"inline_data\":{\"mime_type\":\"audio/wav\",\"data\":\"";
  const char* tail = "\"}}]}],\"generationConfig\":{\"maxOutputTokens\":512}}";

  size_t headLen = strlen(head);
  if (headLen + 16 > g_bodyCap) { gemFail("요청 준비 실패"); return; }
  memcpy(g_body, head, headLen);

  size_t b64Len = b64Encode((const uint8_t*)g_micBuf, wavLen, g_body + headLen);
  size_t tailLen = strlen(tail);
  memcpy(g_body + headLen + b64Len, tail, tailLen);
  const size_t bodyLen = headLen + b64Len + tailLen;

  char url[256];
  snprintf(url, sizeof(url),
           "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent?key=%s",
           GEMINI_MODEL, GEMINI_API_KEY);

  WiFiClientSecure client;
  client.setInsecure();          // 인증서 검증 생략 (시간 동기화 없이 동작)
  client.setHandshakeTimeout(15);

  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(45000);
  http.setUserAgent("StickS3-Monitor/1.0");

  bootScreen("Gemini 에 보내는 중…");
  if (!http.begin(client, url)) { gemFail("요청 생성 실패"); return; }
  http.addHeader("Content-Type", "application/json");

  int code = http.POST((uint8_t*)g_body, bodyLen);
  String resp;
  if (code > 0) resp = http.getString();
  http.end();
  client.stop();

  if (code != 200) {
    String msg = "HTTP " + String(code);
    if (code == 429) msg += " (무료 한도)";
    else if (code == 400) msg += " (요청 형식)";
    else if (code == 403) msg += " (키/권한)";
    // 서버가 준 사유가 있으면 조금 보여준다
    JsonDocument errDoc;
    if (!deserializeJson(errDoc, resp)) {
      const char* m = errDoc["error"]["message"] | "";
      if (strlen(m) > 0) { msg += "\n"; msg += m; }
    }
    gemFail(msg);
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, resp)) { gemFail("응답 해석 실패"); return; }

  String out;
  JsonArray parts = doc["candidates"][0]["content"]["parts"];
  for (JsonVariant p : parts) {
    if (p["thought"] == true) continue;        // 사고 과정은 화면에 쓰지 않는다
    const char* t = p["text"] | "";
    if (t && *t) out += t;
  }
  out.trim();
  if (!out.length()) {
    const char* fr = doc["candidates"][0]["finishReason"] | "";
    gemFail(String("빈 응답 ") + fr);
    return;
  }

  g_gem.text = out;
  g_gem.state = G_ANSWER;
  g_gem.scroll = 0;
  g_gem.scrollAt = millis();
  g_needDraw = true;
}

static void gemTick() {
  switch (g_gem.state) {
    case G_WARMUP:
      // ES8311 은 전원 인가 후 1초 정도 지나야 샘플이 채워진다.
      if (millis() - g_gem.t0 >= GEMINI_MIC_WARMUP_MS) {
        g_gem.state = G_REC;
        g_gem.t0 = millis();
        g_needDraw = true;
      }
      break;

    case G_REC: {
      const size_t maxS = maxSamplesNow();
      if (g_gem.samples < maxS && M5.Mic.isRecording() < 2) {
        size_t chunk = maxS - g_gem.samples;
        if (chunk > 1600) chunk = 1600;                  // 100ms 단위
        int16_t* dst = (int16_t*)((uint8_t*)g_micBuf + 44) + g_gem.samples;
        if (M5.Mic.record(dst, chunk, GEM_SR)) g_gem.samples += chunk;
      }
      if (g_gem.samples >= maxS) { gemStopRec(); }
      break;
    }

    case G_SEND:
      gemSend();      // 블로킹 (수 초). 끝나면 G_ANSWER / G_ERROR 로 바뀐다
      draw();
      break;

    case G_ANSWER:
      if (g_needDraw) draw();
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------------------
//  버튼
// ---------------------------------------------------------------------------
static void handleButtons() {
  const int pages = GEMINI_ENABLED ? 3 : 2;

  if (M5.BtnA.wasClicked()) {
    if (g_gem.state == G_REC || g_gem.state == G_WARMUP) {
      // 녹음 중 실수 방지: 페이지 이동 무시
    } else {
      g_page = (g_page + 1) % pages;
      g_needDraw = true;
    }
  }

  if (M5.BtnB.wasClicked()) {
    if (g_gem.state == G_REC) {
      gemStopRec();
    } else if (g_page == 2) {
      if (g_gem.state != G_WARMUP && g_gem.state != G_SEND) gemStart();
    } else {
      g_nextFetchAt = 0;      // 다음 loop 에서 새로고침
    }
    g_needDraw = true;
  }
}

// ---------------------------------------------------------------------------
//  setup / loop
// ---------------------------------------------------------------------------
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  M5.Display.setRotation(DISPLAY_ROTATION);
  M5.Display.setBrightness(DISPLAY_BRIGHTNESS);
  M5.Display.fillScreen(C_BG);

  g_spr.setPsram(true);
  g_spr.setColorDepth(16);
  g_spriteOk = (g_spr.createSprite(W, H) != nullptr);
  g_ui = g_spriteOk ? (lgfx::LGFXBase*)&g_spr : (lgfx::LGFXBase*)&M5.Display;

  // 스피커는 이 프로젝트에서 쓰지 않는다 (마이크와 ES8311 공유)
  M5.Speaker.end();

  bootScreen("시작 중…");
  if (!wifiConnect()) {
    g_data.error = "Wi-Fi 연결 실패";
  } else {
    fetchStatus();
  }
  g_nextFetchAt = millis() + STATUS_REFRESH_MS;
  draw();
}

void loop() {
  M5.update();
  handleButtons();
  gemTick();

  if ((int32_t)(millis() - g_nextFetchAt) >= 0) {
    if (g_gem.state == G_IDLE || g_gem.state == G_ANSWER || g_gem.state == G_ERROR) {
      g_nextFetchAt = millis() + STATUS_REFRESH_MS;
      if (wifiConnect() && fetchStatus()) {
        // 성공하면 화면 갱신
      }
      g_needDraw = true;
    } else {
      g_nextFetchAt = millis() + 5000;   // Gemini 작업 중이면 잠시 뒤로
    }
  }

  // 녹음 중에는 진행 바를 자주 갱신하고, 그 외에는 변화가 있을 때만 다시 그린다.
  const uint32_t interval = (g_gem.state == G_REC) ? 120 : 2000;
  if (g_needDraw || (millis() - g_lastDrawAt) > interval) {
    draw();
  }
  delay(10);
}
