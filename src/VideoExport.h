#pragma once
#include <QColor>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
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

// 동영상 가져오기 설정
struct ImportOptions {
    double fps = 0;        // 0 = 원본 그대로
    double startSec = 0;
    double durationSec = 0;   // 0 = 끝까지
    int maxFrames = 2000;
    bool fitCanvas = true;    // 캔버스 크기에 맞추기
    QString ffmpeg;
};

struct VideoInfo {
    QSize size;
    double fps = 0;
    double duration = 0;
    bool hasAudio = false;
    bool ok = false;
};

namespace VideoExport {
// ffmpeg 실행 파일 위치를 찾는다 (설정에 지정한 경로 → 실행 파일 옆 → PATH → 흔한 설치 위치). 못 찾으면 빈 문자열.
QString findFfmpeg();
QString findFfprobe();
// 어디를 찾아봤는지 (사용자에게 보여 주려고)
QStringList searchedPaths();
// 설정에 저장된 ffmpeg 경로 (빈 문자열 = 지정 안 함)
QString configuredFfmpeg();
void setConfiguredFfmpeg(const QString& path);
// 성공하면 빈 문자열, 실패하면 오류 메시지. progress(done,total) 가 false 를 돌려주면 취소.
QString run(Document* doc, const QString& outPath, const VideoOptions& opt,
            const std::function<bool(int, int)>& progress = {});

// 두 단계로 나눈 내보내기: 프레임 렌더(문서를 읽으므로 주 스레드에서) → 인코딩(다른 스레드에서 돌려도 된다)
QString renderFrames(Document* doc, const QString& dir, const VideoOptions& opt,
                     const std::function<bool(int, int)>& progress = {});
QString encodeFrames(const QString& dir, const QString& outPath, const VideoOptions& opt, int fpsFallback,
                     const QString& audioPath, double audioStartSec);

// 동영상 읽기
VideoInfo probe(const QString& path, const QString& ffmpegHint = QString());
// 동영상을 PNG 로 풀어서 dir 에 넣는다. 성공하면 빈 문자열, 파일 목록은 outFiles 에.
QString extractFrames(const QString& path, const QString& dir, const ImportOptions& o, QStringList* outFiles,
                      const std::function<bool(int, int)>& progress = {});
// 소리 파일의 파형(0~1 최대값)을 buckets 개로 뽑는다. 실패하면 빈 목록.
QVector<float> audioPeaks(const QString& audioPath, int buckets, const QString& ffmpegHint = QString());
}
