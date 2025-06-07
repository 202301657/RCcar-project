from flask import Flask, Response  # Flask 모듈에서 웹 서버 애플리케이션과 HTTP 응답 객체를 가져옵니다
import cv2  # OpenCV 라이브러리; 영상 캡처 및 처리 기능을 제공합니다
import numpy as np  # NumPy 라이브러리; 배열 및 수치 계산 기능을 제공합니다
import serial  # pySerial 라이브러리; 시리얼 통신을 처리합니다
import time  # 시간 지연 및 시간 측정 기능을 제공합니다
from picamera2 import Picamera2  # PiCamera2 클래스; Raspberry Pi 카메라 제어를 담당합니다

# 시리얼 통신 초기화
ser = serial.Serial('/dev/ttyUSB0', 9600)  # '/dev/ttyUSB0' 포트, 9600 bps 설정으로 시리얼 연결을 엽니다
time.sleep(2)  # 장치 초기화 안정화를 위해 2초 간 대기합니다

# Flask 앱 인스턴스 생성
app = Flask(__name__)  # Flask 애플리케이션 객체를 생성하고 현재 모듈 이름을 지정합니다

# PiCamera2 설정 및 시작
picam2 = Picamera2()  # PiCamera2 객체를 생성합니다
picam2.configure(picam2.create_video_configuration(
    main={"format": "RGB888", "size": (160, 92)}  # 출력 영상 포맷과 해상도를 설정합니다
))
picam2.start()  # 카메라 스트리밍을 시작합니다

# 영상 처리 및 편차(deviation) 계산 함수 정의
def process_frame(frame):  # 각 프레임을 받아 처리 결과 JPEG 바이트를 반환합니다
    height, width, _ = frame.shape  # 프레임의 높이(height)와 너비(width)를 가져옵니다
    center_x = width // 2  # 영상 중앙 x 좌표를 계산합니다

    # 1) 그레이스케일 변환 -> 블러 처리 -> 이진화
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)  # BGR 영상을 그레이스케일로 변환합니다
    blur = cv2.GaussianBlur(gray, (5, 5), 0)  # 노이즈 제거를 위해 5x5 커널로 가우시안 블러를 적용합니다
    _, binary = cv2.threshold(
        blur, 0, 100, cv2.THRESH_BINARY_INV + cv2.THRESH_OTSU
    )  # Otsu 이진화 기법과 반전 옵션을 사용하여 이진 영상을 얻습니다
    # 100으로 설정된 부분을 255에서 값을 조절하여 빛 반사로 인해 라인을 잘 못잡을 때를 방지했습니다.
 
    # 2) 흰색(255)인 픽셀의 y, x 좌표를 배열로 추출합니다
    ys, xs = np.where(binary > 0)  # 이진 영상에서 흰 픽셀 좌표를 분리합니다

    # 예외 처리: 흰 픽셀 수가 너무 적으면 정지 명령 전송
    if len(xs) < 50:
        ser.write(b'S\n')  # 'S' 명령어를 시리얼로 전송합니다
        print("[Send] S")  # 콘솔에 전송 로그를 출력합니다
        _, jpeg = cv2.imencode('.jpg', frame)  # 원본 프레임을 JPEG로 인코딩합니다
        return jpeg.tobytes()  # 인코딩된 바이트를 반환합니다

    # 예외 처리: 흰 픽셀 수가 너무 많으면 직진 명령 전송
    if len(xs) > 2000:
        ser.write(b'N\n')  # 'N' 명령어를 시리얼로 전송합니다
        print("[Send] N")  # 전송 로그 출력
        _, jpeg = cv2.imencode('.jpg', frame)  # 원본 프레임 인코딩
        return jpeg.tobytes()  # 바이트 반환

    # 3) 선형 회귀로 직선 모델(x = a*y + b) 학습
    y_global = ys  # 전체 프레임 기준 y 좌표 배열
    a, b_lin = np.polyfit(y_global, xs, 1)  # 1차 다항 회귀로 기울기(a)와 절편(b_lin)을 구합니다

    # 4) 현재 하단 위치에서의 편차 계산
    y_current = height - 1  # 영상 하단 y 좌표
    x_current = a * y_current + b_lin  # 회귀식에 y값 대입하여 x 예측
    dev_current = x_current - center_x  # 중앙선에서의 차이(편차)

    # 5) 미래(상단) 위치에서의 편차 계산 (예측 목적)
    y_future = int(height * 0.6)  # 영상 높이의 60% 위치 설정
    x_future = a * y_future + b_lin  # 예측 x 좌표 계산
    dev_future = x_future - center_x  # 예측 편차 계산

    # 6) 최종 편차: 현재와 예측 편차를 가중 평균
    w_curr = 0.7  # 현재값 가중치
    w_fut = 0.3   # 예측값 가중치
    dev_final = int(dev_current * w_curr + dev_future * w_fut)  # 최종 정수 편차 계산

    # 7) 계산된 편차를 시리얼로 전송
    message = f"D:{dev_final}\n"  # 문자열 포맷팅
    ser.write(message.encode())  # UTF-8로 인코딩하여 전송
    print(f"[Send] {message.strip()} (curr: {int(dev_current)}, fut: {int(dev_future)})")  # 로그 출력

    # 8) 디버그용 시각화 처리
    debug = frame.copy()  # 원본 프레임 복제
    cv2.line(debug, (center_x, 0), (center_x, height), (255, 0, 0), 1)  # 중앙선을 파란색으로 그림
    cv2.circle(debug, (int(x_future), y_future), 4, (0, 0, 255), -1)  # 예측 지점을 빨간색 원으로 표시
    cv2.circle(debug, (int(x_current), y_current), 4, (0, 255, 0), -1)  # 현재 지점을 초록색 원으로 표시

    # 하단 50% 영역의 이진화 결과를 컬러로 덮어 표시
    roi_low = binary[int(height * 0.5):height, :]  # 하단 절반 이진화 이미지 추출
    roi_color_low = cv2.cvtColor(roi_low, cv2.COLOR_GRAY2BGR)  # BGR 컬러로 변환
    debug[int(height * 0.5):height, :] = roi_color_low  # 원본 디버그 프레임에 덮어씌움

    _, jpeg = cv2.imencode('.jpg', debug)  # 디버그 프레임 인코딩
    return jpeg.tobytes()  # 바이트 반환

# 스트리밍 프레임 생성기 함수
def generate():  # 무한 루프를 돌며 JPEG 바이트 스트림을 생성합니다
    while True:
        frame = picam2.capture_array()  # 카메라로부터 NumPy 배열 프레임을 캡처합니다
        yield (b'--frame\r\n'
               b'Content-Type: image/jpeg\r\n\r\n' +
               process_frame(frame) +  # 처리된 JPEG 바이트 삽입
               b'\r\n')

# 메인 웹 페이지 라우트 정의
@app.route('/')
def index():  # 기본 URL 요청 시 HTML 페이지를 반환합니다
    return '''
    <!DOCTYPE html>
    <html lang="ko">
    <head>
        <meta charset="UTF-8">
        <title>RC카 라인 추적 실시간 뷰어</title>
        <style>
            body {
                background-color: #121212;
                color: #fff;
                font-family: 'Segoe UI', sans-serif;
                text-align: center;
                margin: 0;
                padding: 0;
            }
            h1 {
                margin: 20px;
                font-weight: 500;
            }
            .video-container {
                display: inline-block;
                background: #222;
                padding: 10px;
                border-radius: 12px;
                box-shadow: 0 0 15px rgba(0, 255, 0, 0.3);
            }
            img {
                max-width: 100%;
                border-radius: 8px;
            }
        </style>
    </head>
    <body>
        <h1>RC카 라인 추적 영상</h1>
        <div class="video-container">
            <img src="/video_feed" alt="Video stream">
        </div>
    </body>
    </html>
    '''

# 비디오 피드 라우트 정의
@app.route('/video_feed')
def video_feed():  # '/video_feed' 요청 시 멀티파트 JPEG 스트림을 반환합니다
    return Response(generate(),
                    mimetype='multipart/x-mixed-replace; boundary=frame')

# 서버 실행 코드
if __name__ == '__main__':  # 스크립트가 직접 실행될 때만 서버를 기동합니다
    app.run(host='0.0.0.0', port=5000, debug=False)  # 모든 인터페이스에서 5000번 포트로 실행
