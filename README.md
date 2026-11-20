# StickS3 미니 모니터

M5Stack **StickS3**(ESP32-S3-PICO-1-N8R8, 1.14" 135×240 LCD) 한 대로 만드는 책상 위 미니 대시보드입니다.
집 네트워크에 붙여 두면 5분마다 GitHub의 `status.json`을 받아 **날씨·짧은 메모·관심 종목 시세**를 보여 주고,
3페이지에서는 **버튼 한 번으로 말해서 Gemini 무료 등급에 물어보는 음성 Q&A**를 쓸 수 있습니다.

- 시세/날씨를 고치려면 **GitHub에서 `status.json`만 편집**하면 됩니다. 기기를 다시 굽지 않아도 5분 안에 반영됩니다.
- 공개 저장소의 `status.json`을 HTTPS로 그냥 읽으므로 **기기에 GitHub 토큰을 넣지 않습니다.**
- Gemini는 **무료 등급**(신용카드 등록 불필요)만 사용합니다.

```
Page 1  날씨 + 메모        ← status.json
Page 2  시세 3종           ← status.json
Page 3  Gemini 음성 질문   ← 마이크 → WAV → generateContent
```

---

## 1. 준비물

| 준비물 | 비고 |
| --- | --- |
| M5Stack StickS3 | ESP32-S3-PICO-1-N8R8 / 8MB Flash / 8MB PSRAM |
| USB-C **데이터** 케이블 | 충전 전용 케이블은 인식되지 않습니다 |
| VS Code + PlatformIO IDE | 또는 PlatformIO Core(`pio`) CLI |
| 2.4GHz Wi-Fi | ESP32-S3은 5GHz를 못 잡습니다 |

## 2. 내려받기

```bash
git clone https://github.com/icejung/sticks-monitor.git
cd sticks-monitor
```

## 3. 설정 파일 만들기

`include/config.example.h`를 복사해서 같은 폴더에 **`config.h`**를 만듭니다.

```bash
cp include/config.example.h include/config.h      # Windows: copy
```

`include/config.h`를 열어 두 곳만 채우면 됩니다.

```c
#define WIFI_SSID     "우리집공유기"
#define WIFI_PASSWORD "비밀번호"
#define GEMINI_API_KEY "AIza...에서 시작하는 키"   // 3페이지를 안 쓰면 비워도 됨
```

> `config.h`는 `.gitignore`에 들어 있습니다. Wi-Fi 비밀번호와 API 키가 GitHub에 올라가지 않습니다.
> 절대 `config.example.h`에 실제 값을 적어 커밋하지 마세요.

주요 설정 항목

| 매크로 | 기본값 | 설명 |
| --- | --- | --- |
| `STATUS_URL` | 이 저장소의 `status.json` | 다른 곳에 파일을 두면 주소만 바꾼다 |
| `STATUS_REFRESH_MS` | 300000 (5분) | 새로 받는 주기 |
| `DISPLAY_ROTATION` | 1 | 화면이 거꾸로 보이면 3으로 |
| `DISPLAY_BRIGHTNESS` | 140 | 0~255 |
| `GEMINI_MODEL` | `gemini-3.5-flash-lite` | 무료 등급 모델명 |
| `GEMINI_RECORD_SEC` | 5 | 한 번에 녹음하는 길이(초) |
| `GEMINI_ENABLED` | 1 | 0이면 Gemini 페이지를 숨긴다 |

## 4. 빌드와 업로드

```bash
pio run              # 빌드만
pio run -t upload    # 기기에 굽기
pio device monitor   # 시리얼 로그 (115200)
```

VS Code에서는 하단 상태바의 **→(Upload)** 버튼을 누르면 됩니다.

**업로드가 안 될 때(다운로드 모드)**: StickS3를 USB로 연결한 뒤 **전원 버튼을 길게 눌러 초록 LED가 깜빡이면 놓습니다.**
그 상태에서 다시 `pio run -t upload`를 실행하세요. 업로드가 끝나면 자동으로 재시작됩니다.

## 5. 사용법

| 버튼 | 1·2페이지 | 3페이지 |
| --- | --- | --- |
| **A** (작은 버튼, GPIO11) | 다음 페이지 | 다음 페이지 |
| **B** (작은 버튼, GPIO12) | `status.json` 즉시 새로고침 | **눌러서 말하기** / 녹음 중이면 그만 |

- 전원 버튼: 한 번 누르면 켜기/리셋, 두 번 누르면 끄기.
- **1페이지**: 도시, 현재 기온(큰 글씨), 날씨 상태, 최고/최저, 습도, 미세먼지 등급, 그리고 `note` 한 줄.
- **2페이지**: 종목명 / 가격 / 등락률. 상승은 빨강, 하락은 파랑(국내 관행).
- **3페이지**: B를 누르면 1.2초 마이크 예열 후 **5초 녹음** → Gemini에 보내고 답을 2~3줄로 표시.
  답이 길면 2.5초마다 자동으로 넘어갑니다. 다시 묻고 싶으면 B를 한 번 더 누르세요.
- 오른쪽 위에 배터리 잔량(%)과 아이콘이 항상 표시됩니다.

## 6. status.json 갱신하기

기기에서 읽는 파일은 **`https://raw.githubusercontent.com/icejung/sticks-monitor/main/status.json`** 하나입니다.
GitHub 웹에서 이 파일을 열어 **연필 아이콘(Edit)** → 값 수정 → Commit 하면 5분 안에 기기에 반영됩니다.

### 형식

```json
{
  "updated": "11-20 15:49",
  "note": "금요일 오후 · 코스피 -5.21% 급락 마감",
  "weather": {
    "city": "부천",
    "condition": "맑음",
    "temp": 3,
    "high": 3,
    "low": -3,
    "humidity": 43,
    "air": "좋음"
  },
  "markets": [
    { "symbol": "005930", "name": "삼성전자", "price": 183100, "change": -7.01, "currency": "KRW" },
    { "symbol": "XRP",    "name": "리플 XRP", "price": 0.9695, "change": 0.09,  "currency": "USD" },
    { "symbol": "0005A0", "name": "KODEX S&P500 OTM", "price": 8215, "change": -2.95, "currency": "KRW" }
  ]
}
```

| 필드 | 설명 |
| --- | --- |
| `updated` | 화면 아래 오른쪽에 표시되는 기준 시각(문자열) |
| `note` | 1페이지 맨 아래 두 줄. **짧게** (12px 한글 기준 한 줄 약 19자) |
| `weather.temp/high/low` | 숫자(소수 가능). 화면에는 정수로 반올림해 표시 |
| `weather.air` | 미세먼지 등급 문자열(예: `좋음`, `보통`, `나쁨`). 없으면 숨김 |
| `markets[].name` | 화면에 그대로 나오는 짧은 이름 (와이드 와이드 16자 이내 권장) |
| `markets[].price` | 숫자. `KRW`면 천 단위 쉼표, `USD`면 소수 4자리까지 |
| `markets[].change` | 등락률(%). 부호에 따라 색이 정해짐 |
| `markets` | 최대 4개까지 표시(3개일 때 가장 보기 좋음) |

> ⚠️ **이 저장소는 공개입니다.** `status.json`에는 날씨·시세 같은 공개 정보만 넣으세요.
> 실명, 주소, 전화번호, 계좌번호, 사내 정보, 개인 메일 주소 같은 내용은 절대 넣지 마세요.

## 7. Gemini 무료 API 키 받기

1. <https://aistudio.google.com> 접속 → 구글 계정으로 로그인
2. 왼쪽/상단의 **Get API key** (또는 API Keys) → **Create API key** → 프로젝트 선택 후 생성
3. `AIza...` 로 시작하는 키를 복사해 `include/config.h`의 `GEMINI_API_KEY`에 넣고 다시 업로드

**무료 등급 요약(2026-11 기준)**

- 신용카드 등록이 필요 없고, 입력·출력 토큰이 무료입니다. 대신 **분당/일일 요청 한도(rate limit)** 가 있습니다.
  한도는 프로젝트마다 다르며 AI Studio의 Rate limits 화면에서 확인할 수 있습니다.
- 무료 등급으로 보낸 내용은 **구글 제품 개선에 사용될 수 있습니다**(EEA/영국/스위스 제외).
  회사 기밀·개인정보를 넣어 묻지 마세요.
- 모델: 기본값 `gemini-3.5-flash-lite`.
  - `gemini-2.5-flash`는 deprecated 되어 AI Studio에서 더 이상 선택할 수 없고,
    2026-12-01부터 모든 요청이 `gemini-3.5-flash-lite`로 자동 라우팅됩니다. 그래서 기본값을 3.5 Flash-Lite로 두었습니다.
  - 더 똑똑한 답이 필요하면 `config.h`의 `GEMINI_MODEL`을 `gemini-3.8-flash` 등 무료 등급 모델로 바꾸면 됩니다.
- 하루 한도를 넘기면 화면에 `HTTP 429 (무료 한도)`가 뜹니다. 잠시 뒤 다시 시도하세요.

### 음성 질문은 어떻게 처리되나

1. `M5.Mic`(ES8311 코덱)으로 16kHz / 16bit / 모노로 녹음 → 8MB PSRAM 버퍼
2. 표준 44바이트 WAV 헤더를 붙이고 base64로 인코딩
3. `POST https://generativelanguage.googleapis.com/v1beta/models/{모델}:generateContent?key={키}` 한 번,
   본문에 `inline_data` (`mime_type: "audio/wav"`) + "2~3줄로 짧게 답해라"는 지시문을 함께 전송
4. 응답 JSON에서 `candidates[0].content.parts[].text`만 골라(사고 과정 파트는 제외) 화면에 표시

> ES8311은 스피커와 마이크를 동시에 쓸 수 없습니다. 그래서 녹음 전에 `M5.Speaker.end()`를 호출하고,
> 마이크 예열(약 1.2초, 이 시간 동안 샘플이 0으로 들어올 수 있음) 뒤에 녹음을 시작합니다.

## 8. 문제 해결

| 증상 | 확인할 것 |
| --- | --- |
| 화면이 거꾸로 | `DISPLAY_ROTATION`을 `3`으로 |
| `Wi-Fi 연결 실패` | 2.4GHz SSID인지, 비밀번호 오타, 공유기 MAC 필터 |
| 1페이지에 `status.json HTTP 404` | 저장소 이름/브랜치가 `main`인지, 파일 이름이 `status.json`인지 |
| 값이 안 바뀜 | 커밋 후 최대 5분, 또는 B 버튼으로 즉시 새로고침 |
| `GEMINI_API_KEY 없음` | `include/config.h`에 키를 넣고 다시 업로드 |
| `HTTP 403 (키/권한)` | 키 오타, 또는 해당 모델이 무료 등급에서 제공되지 않음 |
| `HTTP 429 (무료 한도)` | 무료 등급 요청 한도 초과. 잠시 대기 |
| 마이크가 무음 | 마이크 구멍을 가리지 않았는지, 예열 시간(`GEMINI_MIC_WARMUP_MS`)을 늘려 보기 |
| 업로드 실패 | 전원 버튼 길게 눌러 초록 LED 깜빡임 = 다운로드 모드에서 재시도 |

## 9. 파일 구조

```
platformio.ini              # StickS3용 빌드 설정 (8MB Flash / OPI PSRAM)
include/config.example.h    # 설정 템플릿 → config.h 로 복사
src/main.cpp                # 화면·status.json·Gemini 전체 구현
status.json                 # 기기가 읽는 데이터 (공개 정보만)
README.md
```

## 10. 참고

- M5Stack StickS3 공식 문서(핀맵·PlatformIO 설정): <https://docs.m5stack.com/en/core/StickS3>
- M5Unified / M5GFX: <https://github.com/m5stack/M5Unified> · <https://github.com/m5stack/M5GFX>
- Gemini API 문서: <https://ai.google.dev/gemini-api/docs>
