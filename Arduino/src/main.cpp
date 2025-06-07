#include <Servo.h>
#include <PinChangeInterrupt.h>
#include <math.h> // HSV→RGB 변환 등 필요시

// ───── 핀 매핑 정의 ─────
#define ESC_PIN    2          // ESC 제어 핀 (속도)
#define CH2_PIN    7          // 수신기 CH2 → 전진/후진 (PWM 입력)
#define SERVO_PIN  3          // 서보모터 제어 핀 (조향)
#define CH1_PIN    8          // 수신기 CH1 → 좌우 조향 (PWM 입력)
#define CH5_PIN    10         // 수신기 CH5 → 수동/자율 모드 전환
#define CH8_PIN    4          // 수신기 CH8 → RGB LED 제어
#define CH6_PIN    12         // 수신기 CH6 → 사이렌 ON/OFF 제어

Servo esc;    // ESC 속도 제어
Servo steer;  // 조향 제어

// ───── PWM 측정용 인터럽트 변수 ─────
volatile unsigned long ch1_start = 0;
volatile unsigned long ch2_start = 0;
volatile unsigned long ch5_start = 0;
volatile unsigned long ch8_start = 0;
volatile unsigned long ch6_start = 0;

volatile uint16_t ch1_value = 1500;  // 조향
volatile uint16_t ch2_value = 1500;  // 속도
volatile uint16_t ch5_value = 1500;  // 모드 스위치
volatile uint16_t ch8_value = 1500;  // RGB 제어
volatile uint16_t ch6_value = 1500;  // 사이렌 제어

// ───── 시리얼 입력 버퍼 ─────
String inputString   = "";
bool   stringComplete = false;

// ─────────── 수동/자율 공용 LED 핀 정의 ───────────
const uint8_t LEFT_LED_PIN   = 13;  // 좌회전 LED (깜빡이)
const uint8_t RIGHT_LED_PIN  = 11;  // 우회전 LED (깜빡이)

// ─────────── LED 깜빡임용 전역 변수 ───────────
unsigned long leftLastBlinkTime   = 0;
unsigned long rightLastBlinkTime  = 0;
bool         leftLedState         = LOW;
bool         rightLedState        = LOW;

// ─────────── 삼색(RGB) LED 핀 정의 ───────────
// CH8이 위로(>1600) 올라가면 색상 순환
const uint8_t RED_PIN    = 5;   // 빨강 PWM
const uint8_t GREEN_PIN  = 6;   // 초록 PWM
const uint8_t BLUE_PIN   = 9;   // 파랑 PWM

// ─────────── RGB 사이클용 전역 변수 ───────────
unsigned long lastHueUpdateTime   = 0;
int          hue                  = 0;   // 0~359 (HSV 색상 범위)

// ─────────── 스피커용 변수 ───────────
const int SPEAKER_PIN = A0; // 스피커 핀
bool sirenEnabled = false;

// ─────────── 함수 원형 선언 ───────────
void ch1_interrupt();
void ch2_interrupt();
void ch5_interrupt();
void ch8_interrupt();
void ch6_interrupt();
void serialEvent();

// HSV → RGB 변환 함수 (h = 0~359, s=1, v=1 고정)
void hsv_to_rgb(int h, uint8_t &r, uint8_t &g, uint8_t &b) {
  float hf = (float)h / 60.0f;      // 0~359 범위의 Hue를 60으로 나눠 0~6 구간으로 분할 (색상 영역 결정용)
  int   i  = floor(hf);             // 어떤 색상 영역인지 정수로 계산 (예: 0=Red~Yellow, 1=Yellow~Green 등)
  float f  = hf - i;                // 소수점 부분: 색상 간 비율 (보간 계산에 사용)
  float p  = 0.0f;                  // 명도(v=1)와 채도(s=1)이므로 항상 0 (HSV에서 v*(1-s))
  float q  = 1.0f - f;              // 중간 보간값 (점차 감소)
  float t  = f;                     // 중간 보간값 (점차 증가)

  float rf, gf, bf;                 // 변환 후 나올 RGB의 float 버전 (0.0 ~ 1.0)

  // 색상 영역에 따라 RGB를 조합
  switch (i) {
    case 0: rf = 1;  gf = t;  bf = 0;  break;  // 빨강 → 노랑
    case 1: rf = q;  gf = 1;  bf = 0;  break;  // 노랑 → 초록
    case 2: rf = 0;  gf = 1;  bf = t;  break;  // 초록 → 청록
    case 3: rf = 0;  gf = q;  bf = 1;  break;  // 청록 → 파랑
    case 4: rf = t;  gf = 0;  bf = 1;  break;  // 파랑 → 자홍
    default: rf = 1; gf = 0; bf = q;   break;  // 자홍 → 빨강
  }

  // 0.0 ~ 1.0의 값을 0 ~ 255 범위의 RGB 정수 값으로 변환
  r = uint8_t(rf * 255.0f);
  g = uint8_t(gf * 255.0f);
  b = uint8_t(bf * 255.0f);
}

void setup() {
  Serial.begin(9600);

  pinMode(CH1_PIN, INPUT);
  pinMode(CH2_PIN, INPUT);
  pinMode(CH5_PIN, INPUT);
  pinMode(CH6_PIN, INPUT);
  pinMode(SPEAKER_PIN, OUTPUT);

  esc.attach(ESC_PIN);
  steer.attach(SERVO_PIN);

  // LED 핀 출력 모드
  pinMode(LEFT_LED_PIN, OUTPUT);
  pinMode(RIGHT_LED_PIN, OUTPUT);
  digitalWrite(LEFT_LED_PIN, LOW);
  digitalWrite(RIGHT_LED_PIN, LOW);

  // RGB LED 핀 출력 설정
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  analogWrite(RED_PIN, 0);
  analogWrite(GREEN_PIN, 0);
  analogWrite(BLUE_PIN, 0);

  // PWM 입력용 인터럽트
  attachPinChangeInterrupt(
    digitalPinToPinChangeInterrupt(CH1_PIN),
    ch1_interrupt, CHANGE
  );
  attachPinChangeInterrupt(
    digitalPinToPinChangeInterrupt(CH2_PIN),
    ch2_interrupt, CHANGE
  );
  attachPinChangeInterrupt(
    digitalPinToPinChangeInterrupt(CH5_PIN),
    ch5_interrupt, CHANGE
  );
  attachPinChangeInterrupt(
    digitalPinToPinChangeInterrupt(CH8_PIN),
    ch8_interrupt, CHANGE
  );
  attachPinChangeInterrupt(
    digitalPinToPinChangeInterrupt(CH6_PIN),
    ch6_interrupt, CHANGE
  );
}

void loop() { // 메인 반복 함수 시작
  bool autoMode = (ch5_value > 1500);  // ch5 채널이 1500 초과면 자율주행 모드(true), 아니면 수동모드(false)
  unsigned long now = millis(); // 현재 시간을 밀리초 단위로 가져와 now에 저장

  int speed;    // ESC에 보낼 속도 변수 선언
  int angle;    // 서보모터에 보낼 조향 각도 변수 선언

  if (!autoMode) { // ───────────── 수동모드 ─────────────
    speed = constrain(ch2_value, 1440, 1560); // ch2 입력값을 1440~1560 범위로 제한하여 speed에 저장
    angle = constrain(ch1_value, 1000, 2000); // ch1 입력값을 1000~2000 범위로 제한하여 angle에 저장

    esc.writeMicroseconds(speed);   // ESC에 speed 전송
    steer.writeMicroseconds(angle); // 서보모터에 angle 전송
  }
  else { // ───────────── 자율주행모드 ─────────────
    if (stringComplete) { // 시리얼 입력이 완료되었으면
      inputString.trim(); // 입력 문자열 앞뒤 공백 제거

      if (inputString.startsWith("D:")) { // 'D:'로 시작하는 편차 명령일 때
        int deviation = inputString.substring(2).toInt(); // 'D:' 이후 숫자를 정수로 변환

        if (deviation > 0 && deviation < 40) { // 작은 양수 편차
          angle = constrain(1500 - deviation * 40, 1400, 1600); // 편차에 비례해 우회전 각도 계산
          speed = 1560; // 전진 속도 설정
          esc.writeMicroseconds(speed);
          steer.writeMicroseconds(angle);
        }
        else if (deviation < 0 && deviation > -40) { // 작은 음수 편차
          angle = constrain(1500 - deviation * 40, 1400, 1600); // 편차에 비례해 좌회전 각도 계산
          speed = 1560;
          esc.writeMicroseconds(speed);
          steer.writeMicroseconds(angle);
        }
        else if (deviation >= 40 && deviation < 80) { // 중간 정도 양수 편차
          angle = 1000; // 급우회전 각도
          speed = 1550; // 약간 느린 전진 속도
          esc.writeMicroseconds(speed);
          steer.writeMicroseconds(angle);
        }
        else if (deviation <= -40 && deviation > -80) { // 중간 정도 음수 편차
          angle = 2000; // 급좌회전 각도
          speed = 1550;
          esc.writeMicroseconds(speed);
          steer.writeMicroseconds(angle);
        }
        // 너무 큰 편차 시 후진
        else if (deviation >= 80 && deviation < 100) { // 큰 양수 편차
          angle = 1800; // 후진 우회전 각도
          speed = 1440; // 후진 속도
          esc.writeMicroseconds(speed);
          steer.writeMicroseconds(angle);
        }
        else if (deviation <= -80 && deviation > -100) { // 큰 음수 편차
          angle = 1200; // 후진 좌회전 각도
          speed = 1440;
          esc.writeMicroseconds(speed);
          steer.writeMicroseconds(angle);
        }
        else if (deviation >= 100) { // 매우 큰 양수 편차
          angle = 1800;
          speed = 1430; // 더 느린 후진 속도
          esc.writeMicroseconds(speed);
          steer.writeMicroseconds(angle);
        }
        else if (deviation <= -100) { // 매우 큰 음수 편차
          angle = 1200;
          speed = 1430;
          esc.writeMicroseconds(speed);
          steer.writeMicroseconds(angle);
        }
      }
      else if (inputString == "S" || inputString == "N") { // 정지 또는 직진 명령
        angle = 1500; // 중립 각도 (직진)
        speed = 1435; // 정지에 가까운 속도
        esc.writeMicroseconds(speed);
        steer.writeMicroseconds(angle);
      }

      inputString = "";       // 입력 문자열 초기화
      stringComplete = false; // 플래그 초기화
    }
  }

  // ───────────── 수동/자율 공용 LED 제어 ─────────────
  bool isReversing    = (speed < 1500);       // 후진 여부 판단
  bool isTurningLeft  = (!isReversing && angle < 1450);  // 좌회전 여부 판단
  bool isTurningRight = (!isReversing && angle > 1550);  // 우회전 여부 판단

  if (isReversing) { // 후진 중: 양쪽 LED ON
    if (!leftLedState) {
      leftLedState = HIGH;
      digitalWrite(LEFT_LED_PIN, HIGH);
    }
    if (!rightLedState) {
      rightLedState = HIGH;
      digitalWrite(RIGHT_LED_PIN, HIGH);
    }
  }
  else {
    if (isTurningLeft) { // 좌회전 중: 왼쪽 LED 깜빡, 오른쪽 LED OFF
      if (rightLedState) {
        rightLedState = LOW;
        digitalWrite(RIGHT_LED_PIN, LOW);
      }
      if (now - leftLastBlinkTime >= 200) { // 200ms마다 토글
        leftLedState = !leftLedState;
        digitalWrite(LEFT_LED_PIN, leftLedState);
        leftLastBlinkTime = now;
      }
    }
    else if (isTurningRight) { // 우회전 중: 오른쪽 LED 깜빡, 왼쪽 LED OFF
      if (leftLedState) {
        leftLedState = LOW;
        digitalWrite(LEFT_LED_PIN, LOW);
      }
      if (now - rightLastBlinkTime >= 200) {
        rightLedState = !rightLedState;
        digitalWrite(RIGHT_LED_PIN, rightLedState);
        rightLastBlinkTime = now;
      }
    }
    else { // 직진 또는 정지: 양쪽 LED OFF
      if (leftLedState) {
        leftLedState = LOW;
        digitalWrite(LEFT_LED_PIN, LOW);
      }
      if (rightLedState) {
        rightLedState = LOW;
        digitalWrite(RIGHT_LED_PIN, LOW);
      }
    }
  }

  // CH8 > 1600일 때 RGB LED를 경찰등처럼 깜빡임
  static bool policeState = false;        // 현재 색상 상태
  static unsigned long lastPoliceToggle = 0; // 마지막 토글 시간

  if (ch8_value > 1600) {
    if (now - lastPoliceToggle >= 300) {  // 300ms마다 토글
      policeState = !policeState;
      lastPoliceToggle = now;

      if (policeState) {
        digitalWrite(RED_PIN, HIGH);  // 빨강 ON
        digitalWrite(BLUE_PIN, LOW);  // 파랑 OFF
      } else {
        digitalWrite(RED_PIN, LOW);   // 빨강 OFF
        digitalWrite(BLUE_PIN, HIGH); // 파랑 ON
      }
      digitalWrite(GREEN_PIN, LOW);   // 초록 OFF
    }
  }
  else { // CH8 비활성화 시 RGB LED 모두 OFF
    digitalWrite(RED_PIN, LOW);
    digitalWrite(GREEN_PIN, LOW);
    digitalWrite(BLUE_PIN, LOW);
  }

  // CH6 > 1500일 때 사이렌 제어
  if (ch6_value > 1500) {
    static unsigned long lastToneTime = 0; // 마지막 톤 발생 시간
    static int freq = 500;                // 현재 주파수
    static bool up = true;                // 주파수 상승/하강 방향

    if (millis() - lastToneTime >= 5) {   // 5ms마다 주파수 변경
      tone(SPEAKER_PIN, freq);            // 스피커에 톤 출력
      lastToneTime = millis();

      if (up) {
        freq += 10;                       // 주파수 증가
        if (freq >= 1000) up = false;     // 상한 도달 시 방향 전환
      } else {
        freq -= 10;                       // 주파수 감소
        if (freq <= 500) up = true;       // 하한 도달 시 방향 전환
      }
    }
  } else {
    noTone(SPEAKER_PIN); // CH6 비활성화 시 사이렌 OFF
  }
}

void ch1_interrupt() { // CH1 입력 변화 인터럽트
  if (digitalRead(CH1_PIN)) ch1_start = micros();             // 상승 엣지: 시작 시간 기록
  else                      ch1_value = micros() - ch1_start; // 하강 엣지: 펄스 폭 계산
}

void ch2_interrupt() { // CH2 인터럽트
  if (digitalRead(CH2_PIN)) ch2_start = micros();
  else                      ch2_value = micros() - ch2_start;
}

void ch5_interrupt() { // CH5 인터럽트
  if (digitalRead(CH5_PIN)) ch5_start = micros();
  else                      ch5_value = micros() - ch5_start;
}

void ch8_interrupt() { // CH8 인터럽트
  if (digitalRead(CH8_PIN)) ch8_start = micros();
  else                      ch8_value = micros() - ch8_start;
}

void ch6_interrupt() { // CH6 인터럽트
  if (digitalRead(CH6_PIN)) ch6_start = micros();
  else                      ch6_value = micros() - ch6_start;
}

void serialEvent() { // 시리얼 이벤트 핸들러
  while (Serial.available()) {        // 수신 데이터가 있으면
    char inChar = (char)Serial.read(); // 한 문자 읽기
    if (inChar == '\n') {             // 줄바꿈 문자이면
      stringComplete = true;          // 문자열 완전 수신 플래그 설정
    } else {
      inputString += inChar;          // 입력 버퍼에 문자 추가
    }
  }
}
