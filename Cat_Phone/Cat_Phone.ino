/*
 * ============================================================
 *  📞 전화기 프로젝트 v4
 *  Arduino UNO + VS1053 + L298N + PAM8406
 * ============================================================
 *
 *  동작:
 *    [버튼 HIGH = 수화기 내려놓음]
 *      → 5분 대기 → 모터 벨 패턴 10초 (2초on+3초off)
 *      → 10초 후 자동 정지 → 다시 5분 대기 반복
 *
 *    [버튼 LOW = 수화기 들어올림]
 *      → 모터 즉시 정지
 *      → 1초 후 track001~004 중 랜덤 1곡 1회 재생
 *
 *    [버튼 다시 HIGH = 수화기 내려놓음]
 *      → MP3 정지 → 5분 타이머 리셋 → 반복
 *
 * ============================================================
 */  

#include <SPI.h>
#include <SdFat.h>
#include <vs1053_SdFat.h>

// ============================================================
//  ⏱️ 조절 가능한 설정값
// ============================================================

unsigned long WAIT_TIME_MS     = 300000;  // 벨 울리기까지 대기 (5분 = 300000)
unsigned long RING_DURATION_MS = 10000;   // 벨 총 울림 시간 (10초)
unsigned long RING_ON_MS       = 2000;    // 벨 패턴 - 울림 (2초)
unsigned long RING_OFF_MS      = 3000;    // 벨 패턴 - 멈춤 (3초)
unsigned long PICKUP_DELAY_MS  = 1000;    // 수화기 들고 MP3까지 지연 (1초)

uint8_t MOTOR_SPEED    = 100;    // 모터 속도 (0~255)
uint8_t MP3_VOLUME     = 2;      // MP3 볼륨 (0=최대)
uint8_t TRACK_COUNT    = 4;      // MP3 파일 개수 (track001~004)

// ============================================================
//  📌 핀 정의
// ============================================================
#define BUTTON_PIN   10
#define MOTOR_IN1     3
#define MOTOR_IN2     4
#define MOTOR_ENA     5
#define SD_SEL        9

// ============================================================
//  상태 머신
// ============================================================
enum State {
  STATE_IDLE,         // 버튼 HIGH → 5분 대기 중
  STATE_RINGING,      // 모터 벨 패턴 (최대 10초)
  STATE_PICKUP_WAIT,  // 수화기 들음 → MP3 전 대기
  STATE_PLAYING       // MP3 1회 재생 중
};

// ============================================================
//  전역 변수
// ============================================================
SdFat sd;
vs1053 MP3player;

State currentState = STATE_IDLE;
unsigned long timerStart = 0;
bool motorRunning = false;

// 벨 패턴
unsigned long ringStartTime = 0;     // 벨 시작 시각 (10초 카운트용)
unsigned long ringPatternStart = 0;  // 패턴 on/off 전환 시각
bool ringPhaseOn = true;

// 수화기 지연
unsigned long pickupTime = 0;

// 디바운스
bool lastButtonState = LOW;
bool buttonState     = LOW;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 50;

// ============================================================
//  모터 제어
// ============================================================
void motorOn() {
  if (!motorRunning) {
    digitalWrite(MOTOR_IN1, HIGH);
    digitalWrite(MOTOR_IN2, LOW);
    analogWrite(MOTOR_ENA, MOTOR_SPEED);
    motorRunning = true;
  }
}

void motorOff() {
  if (motorRunning) {
    digitalWrite(MOTOR_IN1, LOW);
    digitalWrite(MOTOR_IN2, LOW);
    analogWrite(MOTOR_ENA, 0);
    motorRunning = false;
  }
}

// ============================================================
//  MP3 제어
// ============================================================
void mp3PlayRandom() {
  if (MP3player.isPlaying()) {
    MP3player.stopTrack();
    delay(100);
  }

  // 1 ~ TRACK_COUNT 중 랜덤
  uint8_t track = random(1, TRACK_COUNT + 1);

  uint8_t result = MP3player.playTrack(track);
  if (result == 0) {
    Serial.print(F("[MP3] track00"));
    Serial.print(track);
    Serial.println(F(".mp3 재생"));
  } else {
    Serial.print(F("[MP3] 에러: "));
    Serial.println(result);
  }
}

void mp3Stop() {
  if (MP3player.isPlaying()) {
    MP3player.stopTrack();
    Serial.println(F("[MP3] 정지"));
  }
}

// ============================================================
//  버튼 (디바운스)
// ============================================================
bool readButton() {
  bool reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != buttonState) {
      buttonState = reading;
    }
  }
  lastButtonState = reading;
  return buttonState;
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println(F(""));
  Serial.println(F("========================================"));
  Serial.println(F("  📞 전화기 프로젝트 v4"));
  Serial.println(F("========================================"));

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
  pinMode(MOTOR_ENA, OUTPUT);
  motorOff();

  // 랜덤 시드 (미연결 아날로그 핀의 노이즈)
  randomSeed(analogRead(A0));

  Serial.print(F("[INIT] SD... "));
  if (!sd.begin(SD_SEL, SPI_HALF_SPEED)) {
    sd.initErrorHalt();
  }
  Serial.println(F("OK"));

  Serial.print(F("[INIT] VS1053... "));
  uint8_t result = MP3player.begin();
  if (result != 0) {
    Serial.print(F("에러: "));
    Serial.println(result);
  } else {
    Serial.println(F("OK"));
  }
  MP3player.setVolume(MP3_VOLUME, MP3_VOLUME);

  Serial.print(F("  벨 간격: "));
  Serial.print(WAIT_TIME_MS / 60000);
  Serial.println(F("분"));
  Serial.print(F("  벨 지속: "));
  Serial.print(RING_DURATION_MS / 1000);
  Serial.println(F("초"));
  Serial.print(F("  트랙 수: "));
  Serial.println(TRACK_COUNT);
  Serial.println(F("========================================"));

  buttonState = digitalRead(BUTTON_PIN);
  lastButtonState = buttonState;

  if (buttonState == HIGH) {
    currentState = STATE_IDLE;
    timerStart = millis();
    Serial.println(F("[상태] 수화기 내려짐 → 대기"));
  } else {
    currentState = STATE_PICKUP_WAIT;
    pickupTime = millis();
    Serial.println(F("[상태] 수화기 들림 → MP3 대기"));
  }
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  bool btn = readButton();

  switch (currentState) {

    // --------------------------------------------------------
    //  STATE_IDLE: 버튼 HIGH → 5분 대기
    // --------------------------------------------------------
    case STATE_IDLE:
      if (btn == HIGH) {
        if ((millis() - timerStart) >= WAIT_TIME_MS) {
          // 5분 경과 → 벨 시작
          currentState = STATE_RINGING;
          ringStartTime = millis();
          ringPatternStart = millis();
          ringPhaseOn = true;
          motorOn();
          Serial.println(F("[상태] 벨 시작 🔔"));
        }
      }
      else {
        // 대기 중 수화기 들기
        currentState = STATE_PICKUP_WAIT;
        pickupTime = millis();
        Serial.println(F("[상태] 대기 중 수화기 들음"));
      }
      break;

    // --------------------------------------------------------
    //  STATE_RINGING: 벨 패턴 (10초간 2초on+3초off)
    // --------------------------------------------------------
    case STATE_RINGING:
      if (btn == LOW) {
        // 수화기 들어올림 → 모터 정지 + MP3 대기
        motorOff();
        currentState = STATE_PICKUP_WAIT;
        pickupTime = millis();
        Serial.println(F("[상태] 수화기 들음 → 모터 정지"));
      }
      else {
        // 10초 경과 체크
        if ((millis() - ringStartTime) >= RING_DURATION_MS) {
          // 10초 끝 → 모터 정지 + 다시 5분 대기
          motorOff();
          timerStart = millis();
          currentState = STATE_IDLE;
          Serial.println(F("[상태] 벨 10초 종료 → 다시 5분 대기"));
        }
        else {
          // 벨 패턴 실행
          unsigned long elapsed = millis() - ringPatternStart;

          if (ringPhaseOn) {
            motorOn();
            if (elapsed >= RING_ON_MS) {
              motorOff();
              ringPhaseOn = false;
              ringPatternStart = millis();
            }
          }
          else {
            motorOff();
            if (elapsed >= RING_OFF_MS) {
              motorOn();
              ringPhaseOn = true;
              ringPatternStart = millis();
            }
          }
        }
      }
      break;

    // --------------------------------------------------------
    //  STATE_PICKUP_WAIT: 수화기 든 후 1초 대기
    // --------------------------------------------------------
    case STATE_PICKUP_WAIT:
      if (btn == HIGH) {
        // 대기 중 수화기 다시 내려놓음
        currentState = STATE_IDLE;
        timerStart = millis();
        Serial.println(F("[상태] 수화기 내려놓음 → 대기 리셋"));
      }
      else {
        if ((millis() - pickupTime) >= PICKUP_DELAY_MS) {
          // 지연 완료 → 랜덤 MP3 1회 재생
          mp3PlayRandom();
          currentState = STATE_PLAYING;
          Serial.println(F("[상태] MP3 1회 재생 🎵"));
        }
      }
      break;

    // --------------------------------------------------------
    //  STATE_PLAYING: MP3 1회 재생 중
    // --------------------------------------------------------
    case STATE_PLAYING:
      if (btn == HIGH) {
        // 수화기 내려놓음 → 정지 + 5분 타이머 리셋
        mp3Stop();
        motorOff();
        timerStart = millis();
        currentState = STATE_IDLE;
        Serial.println(F("[상태] 수화기 내려놓음 → 정지, 5분 대기 시작"));
      }
      // MP3 끝나도 반복 안 함 (1회만)
      break;
  }

  delay(10);
}
