#pragma once
// ---------------------------------------------------------------------------
//  config.example.h
//
//  사용법:
//    1) 이 파일을 같은 폴더의 config.h 로 복사한다.
//         cp include/config.example.h include/config.h   (Windows: copy)
//    2) GEMINI_API_KEY 를 채운다. (3페이지 음성 질문을 쓸 때만)
//       Wi-Fi 는 휴대폰에서 설정하므로 비워 두어도 된다(아래 설명).
//    3) config.h 는 .gitignore 에 들어 있어 GitHub에 올라가지 않는다.
//       (비밀번호·API 키를 절대 커밋하지 마세요)
// ---------------------------------------------------------------------------

// -------- Wi-Fi (2.4GHz만 지원) --------
// 권장: 휴대폰에서 설정한다.
//   1) 기기를 켜면 Wi-Fi 가 없을 때/연결에 실패할 때 AP "StickS3-Setup" 가 열린다.
//   2) 휴대폰 Wi-Fi 에서 StickS3-Setup 에 접속 → http://192.168.4.1
//   3) 주변 Wi-Fi 를 고르고 비밀번호를 넣으면 기기에 저장(NVS)되고 재시작한다.
//   나중에 바꿀 때는 B 버튼을 3초 길게 누른다.
//
// 아래 두 줄은 "예비" 값이다. 휴대폰에서 저장한 값이 없을 때만 쓰인다.
// 비워 두면 첫 부팅 때 자동으로 휴대폰 설정 화면이 뜬다.
#define WIFI_SSID     ""
#define WIFI_PASSWORD ""

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

// -------- 3페이지 스피커 음성 출력 (Gemini TTS) --------
// 답변 텍스트를 Gemini TTS 로 합성해 StickS3 내장 스피커로 읽어 준다.
// 음성 합성이 실패(무료 한도 초과·네트워크 오류)해도 화면의 텍스트 답변은 그대로 남고
// 조용히 텍스트 전용으로 동작한다. 끄려면 0.
#define GEMINI_TTS_ENABLED 1

// 오디오 출력(responseModalities: ["AUDIO"])을 지원하는 TTS 모델.
// 응답은 base64 로 인코딩된 16bit 모노 24kHz PCM 이다.
#define GEMINI_TTS_MODEL "gemini-2.5-flash-preview-tts"

// 목소리 이름. Kore(한국어, 차분한 톤) 외에 Puck·Charon·Fenrir·Aoede 등이 있다.
#define GEMINI_TTS_VOICE "Kore"

// 스피커 볼륨 0~255. StickS3 공식 안내: 배터리로 쓸 때는 75%(≈191) 이하를 권장한다
// (과전류로 기기가 재부팅될 수 있음). 180 ≈ 70%.
#define SPEAKER_VOLUME 180

// TTS 출력 기본 샘플레이트. 응답 mimeType 에 rate= 값이 있으면 그 값을 우선한다.
#define GEMINI_TTS_SR 24000
