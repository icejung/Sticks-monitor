#pragma once
// ---------------------------------------------------------------------------
//  config.example.h
//
//  사용법:
//    1) 이 파일을 같은 폴더의 config.h 로 복사한다.
//         cp include/config.example.h include/config.h   (Windows: copy)
//    2) 아래 WIFI_SSID / WIFI_PASSWORD / GEMINI_API_KEY 를 채운다.
//    3) config.h 는 .gitignore 에 들어 있어 GitHub에 올라가지 않는다.
//       (비밀번호·API 키를 절대 커밋하지 마세요)
// ---------------------------------------------------------------------------

// -------- Wi-Fi (2.4GHz만 지원) --------
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// -------- GitHub에 올린 status.json --------
// 저장소를 포크했거나 파일 위치가 다르면 이 주소만 바꾸면 된다.
// 공개 저장소이므로 토큰 없이 HTTPS로 바로 받아온다.
#define STATUS_URL "https://raw.githubusercontent.com/icejung/sticks-monitor/main/status.json"

// status.json 새로 받아오는 주기 (밀리초). 기본 5분.
#define STATUS_REFRESH_MS (5UL * 60UL * 1000UL)

// -------- 화면 --------
// 1 = 가로(240x135), 3 = 가로(뒤집힘). 화면이 거꾸로 보이면 3으로 바꾼다.
#define DISPLAY_ROTATION 1
// 밝기 0~255
#define DISPLAY_BRIGHTNESS 140

// -------- Gemini 무료 등급 음성 Q&A --------
// aistudio.google.com 에서 무료로 발급 (신용카드 등록 불필요, rate limit만 적용)
#define GEMINI_API_KEY "YOUR_GEMINI_API_KEY"

// 2026-11 기준 무료 등급에서 쓸 수 있는 최신 Flash-Lite 모델.
//   - gemini-3.5-flash-lite : 기본값 (무료, 오디오 입력 지원)
//   - gemini-3.8-flash      : 더 똑똑하지만 응답이 느리고 API 파라미터가 다름
//   - gemini-2.5-flash      : deprecated. 2026-12-01부터 3.5-flash-lite로 자동 라우팅
#define GEMINI_MODEL "gemini-3.5-flash-lite"

// 한 번에 녹음하는 길이(초). 5초 = 16kHz/16bit 모노 약 160KB → 8MB PSRAM에 충분.
#define GEMINI_RECORD_SEC 5

// Gemini에 함께 보내는 지시문. 작은 화면에 맞게 짧게 답하도록 한다.
#define GEMINI_PROMPT \
  "너는 작은 LCD(240x135)를 쓰는 휴대기기의 비서다. 첨부한 한국어 음성 질문을 듣고 한국어로 답해라. " \
  "조건: 2~3줄, 공백 포함 60자 이내, 목록기호(*)나 마크다운 없이 평문으로, 인사말 없이 핵심만."

// 기능 끄기: 0으로 두면 Gemini 페이지를 숨기고 컴파일 크기도 줄어든다.
#define GEMINI_ENABLED 1
