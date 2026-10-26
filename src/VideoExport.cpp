#include "VideoExport.h"
#include "Document.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <algorithm>

namespace VideoExport {

QString findFfmpeg() {
#ifdef Q_OS_WIN
    const QString exe = "ffmpeg.exe";
#else
    const QString exe = "ffmpeg";
#endif
    const QString beside = QDir(QCoreApplication::applicationDirPath()).filePath(exe);
    if (QFile::exists(beside)) return beside;
    return QStandardPaths::findExecutable("ffmpeg");
}

QString run(Document* doc, const QString& outPath, const VideoOptions& o, const std::function<bool(int, int)>& progress) {
    const QString ff = o.ffmpeg.isEmpty() ? findFfmpeg() : o.ffmpeg;
    if (ff.isEmpty())
        return QStringLiteral("ffmpeg 를 찾을 수 없습니다.\nffmpeg.exe 를 ONY 실행 파일(ony.exe)과 같은 폴더에 두거나, ffmpeg 를 설치해 PATH 에 추가하세요.");
    const int n = doc->frameCount();
    const int from = std::clamp(o.from, 0, n - 1);
    const int to = std::clamp(o.to < 0 ? n - 1 : o.to, from, n - 1);
    const int total = to - from + 1;

    const bool seq = o.format == "png" || o.format == "jpg" || o.format == "bmp" || o.format == "tiff";
    const int fpsOut = o.fps > 0 ? o.fps : doc->fps();
    // 알파를 지원하지 않는 형식에서 투명 배경이면 흰색으로 대체한다
    QColor bg = o.background;
    const bool alphaOk = o.format == "png" || o.format == "tiff" || o.format == "mov" || o.format == "webm";
    if (bg.alpha() == 0 && !alphaOk) bg = Qt::white;

    if (seq) { // 이미지 시퀀스: ffmpeg 없이 바로 저장한다
        QDir().mkpath(outPath);
        for (int f = from; f <= to; ++f) {
            const QImage img = doc->renderFrame(f, bg, o.useCamera);
            QImage save = img;
            if (o.scalePercent != 100) save = img.scaled(img.size() * o.scalePercent / 100, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            const QString ext = o.format == "tiff" ? "tif" : o.format;
            const QString path = QDir(outPath).filePath(QString("%1_%2.%3").arg(o.baseName).arg(f - from + 1, 4, 10, QChar('0')).arg(ext));
            const bool ok = o.format == "jpg" ? save.convertToFormat(QImage::Format_RGB32).save(path, "JPG", 95) : save.save(path);
            if (!ok) return QString("프레임 %1 을 저장하지 못했습니다.").arg(f + 1);
            if (progress && !progress(f - from + 1, total)) return QStringLiteral("취소됨");
        }
        return {};
    }

    QTemporaryDir tmp;
    if (!tmp.isValid()) return QStringLiteral("임시 폴더를 만들 수 없습니다.");
    for (int f = from; f <= to; ++f) {
        if (!doc->renderFrame(f, bg, o.useCamera).save(QDir(tmp.path()).filePath(QString("frame_%1.png").arg(f - from + 1, 5, 10, QChar('0'))), "PNG"))
            return QString("프레임 %1 을 렌더링하지 못했습니다.").arg(f + 1);
        if (progress && !progress(f - from + 1, total + 1)) return QStringLiteral("취소됨");
    }

    const int crf[] = {16, 21, 27};
    const QString q = QString::number(crf[std::clamp(o.quality, 0, 2)]);
    const int sp = std::clamp(o.scalePercent, 10, 400);
    // 가로/세로를 짝수로 맞춘다 (yuv420p 요구사항)
    const QString scale = QString("scale=trunc(iw*%1/200)*2:trunc(ih*%1/200)*2:flags=lanczos").arg(sp);

    QStringList args{"-y", "-hide_banner", "-loglevel", "error", "-framerate", QString::number(fpsOut), "-i",
                     QDir(tmp.path()).filePath("frame_%05d.png")};
    const bool wantAudio = o.audio && o.format != "gif" && !doc->audioPath().isEmpty() && QFile::exists(doc->audioPath());
    if (wantAudio) args << "-ss" << QString::number(double(from) / doc->fps(), 'f', 3) << "-i" << doc->audioPath();
    if (o.format == "gif") {
        args << "-vf" << scale + ",split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse" << "-loop" << "0";
    } else if (o.format == "webm") {
        args << "-vf" << scale << "-c:v" << "libvpx-vp9" << "-crf" << q << "-b:v" << "0"
             << "-pix_fmt" << (o.background.alpha() == 0 ? "yuva420p" : "yuv420p");
        if (wantAudio) args << "-c:a" << "libopus" << "-shortest";
    } else if (o.format == "mov") {
        args << "-vf" << scale << "-c:v" << "prores_ks" << "-profile:v" << (o.background.alpha() == 0 ? "4" : (o.quality == 0 ? "3" : (o.quality == 1 ? "2" : "1")))
             << "-pix_fmt" << (o.background.alpha() == 0 ? "yuva444p10le" : "yuv422p10le");
        if (wantAudio) args << "-c:a" << "pcm_s16le" << "-shortest";
    } else { // mp4
        args << "-vf" << scale << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << q << "-pix_fmt" << "yuv420p"
             << "-movflags" << "+faststart";
        if (wantAudio) args << "-c:a" << "aac" << "-b:a" << "192k" << "-shortest";
    }
    args << outPath;

    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(ff, args);
    if (!p.waitForStarted(5000)) return QString("ffmpeg 를 실행할 수 없습니다: %1").arg(ff);
    while (!p.waitForFinished(120)) {
        if (progress && !progress(total, total + 1)) {
            p.kill();
            p.waitForFinished(2000);
            return QStringLiteral("취소됨");
        }
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        const QString log = QString::fromLocal8Bit(p.readAll()).trimmed();
        return QString("ffmpeg 오류 (코드 %1):\n%2").arg(p.exitCode()).arg(log.right(600));
    }
    if (progress) progress(total + 1, total + 1);
    return {};
}

} // namespace VideoExport
