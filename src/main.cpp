#include <Servo.h>
#include <PinChangeInterrupt.h>  // 인터럽트 핀 확장용 라이브러리

#define ESC_PIN 2         // ESC 제어 핀 (속도)
#define SERVO_PIN 3       // 서보모터 제어 핀 (조향)
#define CH1_PIN 7         // 수신기 CH1 → 전진/후진 (상하)
#define CH2_PIN 8         // 수신기 CH2 → 조향 (좌우)

volatile uint16_t ch1_val = 1500;  // 초기값: 정지
volatile uint16_t ch2_val = 1500;

volatile unsigned long ch1_start = 0;
volatile unsigned long ch2_start = 0;

Servo esc;       // 전진/후진 제어용
Servo steering;  // 조향 제어용

void ch1_interrupt();
void ch2_interrupt();

void setup() {
  pinMode(CH1_PIN, INPUT);
  pinMode(CH2_PIN, INPUT);

  attachPinChangeInterrupt(digitalPinToPinChangeInterrupt(CH1_PIN), ch1_interrupt, CHANGE);
  attachPinChangeInterrupt(digitalPinToPinChangeInterrupt(CH2_PIN), ch2_interrupt, CHANGE);

  esc.attach(ESC_PIN);
  steering.attach(SERVO_PIN);

  Serial.begin(9600);

  esc.writeMicroseconds(1500);  // 정지 상태로 초기화
  steering.write(90);           // 조향 중립
  delay(2000);                  // ESC 초기화 대기
}

void loop() {
  // 전진/후진 속도 계산 (1500 기준 ±50 제한)
  int delta = ch1_val - 1500;
  delta = constrain(delta, -500, 500);            // 부드럽게 제한
  int limitedThrottle = 1500 + delta;
  esc.writeMicroseconds(limitedThrottle);       // 방향

  // 조향 각도 계산 (입력값 1000~2000 → 30~150도로 매핑)
  int angle = constrain(ch2_val, 1430, 1560);
  int speed = angle * 0.2;
  steering.write(angle);                        // 속도

  // 디버깅용 출력
  Serial.print("CH1 PWM: ");
  Serial.print(ch1_val);
  Serial.print("  Throttle: ");
  Serial.print(limitedThrottle);
  Serial.print("  CH2 PWM: ");
  Serial.print(ch2_val);
  Serial.print("  Steering Angle: ");
  Serial.println(angle);
}

void ch1_interrupt() {
  if (digitalRead(CH1_PIN) == HIGH) {
    ch1_start = micros();
  } else {
    ch1_val = micros() - ch1_start;
  }
}

void ch2_interrupt() {
  if (digitalRead(CH2_PIN) == HIGH) {
    ch2_start = micros();
  } else {
    ch2_val = micros() - ch2_start;
  }
}
