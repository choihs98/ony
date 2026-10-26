#pragma once
#include <QColor>
#include <QString>
#include <functional>

class Document;

// 프레임을 PNG 로 렌더링한 뒤 ffmpeg 로 동영상(MP4/MOV/WebM/GIF)으로 묶는다.
struct VideoOptions {
    QString format = "mp4"; // mp4 | mov | webm | gif | png | jpg | bmp | tiff (뒤의 넷은 이미지 시퀀스)
    int from = 0, to = -1;  // 프레임 범위 (to < 0 이면 끝까지)
    int quality = 1;        // 0 = 높음, 1 = 보통, 2 = 낮음
    int scalePercent = 100; // 출력 크기 %
    bool audio = true;      // 프로젝트에 소리가 있으면 함께 넣기
    QColor background = Qt::white; // alpha 0 = 투명 (mov, webm, png 시퀀스에서만 의미 있음)
    bool useCamera = true;  // 카메라가 켜져 있으면 카메라 시점으로 내보내기
    int fps = 0;            // 0 = 프로젝트 프레임 속도
    QString baseName = "frame"; // 이미지 시퀀스 파일 이름 앞부분
    QString ffmpeg;         // 비어 있으면 자동 탐색
};

namespace VideoExport {
// ffmpeg 실행 파일 위치를 찾는다 (실행 파일 옆 → PATH). 못 찾으면 빈 문자열.
QString findFfmpeg();
// 성공하면 빈 문자열, 실패하면 오류 메시지. progress(done,total) 가 false 를 돌려주면 취소.
QString run(Document* doc, const QString& outPath, const VideoOptions& opt,
            const std::function<bool(int, int)>& progress = {});
}
