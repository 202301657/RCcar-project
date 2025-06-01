#include <Servo.h>
#include <PinChangeInterrupt.h>

#define ESC_PIN    2
#define CH2_PIN    7   // 수신기 → CH2 (전진/후진)

#define SERVO_PIN  3
#define CH1_PIN    8   // 수신기 → CH1 (좌우 조향)

#define CH5_PIN    10  // 수신기 CH5 → 모드 전환 (SWA 스위치)

Servo esc;    // 속도
Servo steer;  // 조향

volatile unsigned long ch1_start = 0;
volatile unsigned long ch2_start = 0;
volatile unsigned long ch5_start = 0;

volatile uint16_t ch1_value = 1500;
volatile uint16_t ch2_value = 1500;
volatile uint16_t ch5_value = 1500;

const uint8_t LED_PIN   = 11;   // LED를 연결할 핀 (핀 11번)
String inputString = "";
bool stringComplete  = false;

// ─────────── LED 깜빡임용 전역 변수 ───────────
unsigned long lastBlinkTime = 0;  
bool ledState             = LOW;  

void ch1_interrupt();
void ch2_interrupt();
void ch5_interrupt();
void serialEvent();

void setup() {
  Serial.begin(9600);

  pinMode(CH1_PIN, INPUT);
  pinMode(CH2_PIN, INPUT);
  pinMode(CH5_PIN, INPUT);

  // LED_PIN을 출력 모드로 설정 (핀 11번)
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

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

  esc.attach(ESC_PIN);
  steer.attach(SERVO_PIN);
}

void loop() {
  bool autoMode = (ch5_value > 1500);

  if (!autoMode) {  // ───────────── 수동모드 ─────────────
    int speed = constrain(ch2_value, 1450, 1570);
    esc.writeMicroseconds(speed);

    int angle = constrain(ch1_value, 1000, 2000);
    steer.writeMicroseconds(angle);

    // 수동모드일 때 LED는 꺼 두기
    ledState = LOW;
    digitalWrite(LED_PIN, ledState);

    delay(10);
    return;
  }

  // ───────────── 자율주행모드 ─────────────
  if (stringComplete) {
    inputString.trim();

    if (inputString.startsWith("D:")) {
      int deviation = inputString.substring(2).toInt();

      if (deviation > 0 && deviation < 30) {
        int angle = 1500 - deviation * 40;
        angle = constrain(angle, 1400, 1600);
        steer.writeMicroseconds(angle);
        esc.writeMicroseconds(1540);

        // 이 구간에서는 LED 끔
        ledState = LOW;
        digitalWrite(LED_PIN, ledState);
      }
      else if (deviation < 0 && deviation > -30) {
        int angle = 1500 - deviation * 40;
        angle = constrain(angle, 1400, 1600);
        steer.writeMicroseconds(angle);
        esc.writeMicroseconds(1540);

        // 이 구간에서는 LED 끔
        ledState = LOW;
        digitalWrite(LED_PIN, ledState);
      }
      else if (deviation >= 30 && deviation < 60) {
        steer.writeMicroseconds(1000);
        esc.writeMicroseconds(1530);

        // ─────────── 0.2초 간격으로 LED 깜빡이기 ───────────
        unsigned long now = millis();
        if (now - lastBlinkTime >= 200) {
          ledState = !ledState;
          digitalWrite(LED_PIN, ledState);
          lastBlinkTime = now;
        }
      }
      else if (deviation <= -30 && deviation > -60) {
        steer.writeMicroseconds(2000);
        esc.writeMicroseconds(1530);

        // 이 구간에서는 LED 끔
        ledState = LOW;
        digitalWrite(LED_PIN, ledState);
      }

      // 모든 조건 후 한 번 더 전진 신호를 보내 줍니다.
      esc.writeMicroseconds(1540);

      // deviation이 너무 크면 후진
      if (deviation >= 60 && deviation < 100) {
        steer.writeMicroseconds(1800);
        esc.writeMicroseconds(1440); // 살짝 후진 
      }
      else if (deviation <= -60 && deviation > -100) {
        steer.writeMicroseconds(1200);
        esc.writeMicroseconds(1440); // 살짝 후진 
      }
    }
    else if (inputString == "S") {
      steer.writeMicroseconds(1500);
      esc.writeMicroseconds(1440);
      Serial.println("[AUTO] Stop (No Line)");

      // 라인 미검출 구간에서는 LED도 꺼 둡니다.
      ledState = LOW;
      digitalWrite(LED_PIN, ledState);
    }
    else {
      // 예기치 않은 문자열이 들어올 때는 LED도 꺼 둡니다.
      ledState = LOW;
      digitalWrite(LED_PIN, ledState);
    }

    inputString = "";
    stringComplete = false;
  }

  delay(20);
}

void ch1_interrupt() {
  if (digitalRead(CH1_PIN)) ch1_start = micros();
  else                      ch1_value = micros() - ch1_start;
}

void ch2_interrupt() {
  if (digitalRead(CH2_PIN)) ch2_start = micros();
  else                      ch2_value = micros() - ch2_start;
}

void ch5_interrupt() {
  if (digitalRead(CH5_PIN)) ch5_start = micros();
  else                      ch5_value = micros() - ch5_start;
}

// ───────────── 시리얼 수신 인터럽트 처리 ─────────────
void serialEvent() {
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    if (inChar == '\n') {
      stringComplete = true;
    } else {
      inputString += inChar;
    }
  }
}

// ───────────── pinchangeInterrupt 예제 ─────────────
// (이 코드에서는 PCINT_PIN = 11을 사용하지 않습니다.)
// 필요하다면 attachPinChangeInterrupt를 추가하거나
// pcintISR()를 활용해 다른 기능을 구현하시면 됩니다.
void pcintISR() {
  // 예시: 핀 11의 변화 감지 시 LED 토글
  bool pinState = digitalRead(LED_PIN);
  ledState = !pinState;
  digitalWrite(LED_PIN, ledState);
}
