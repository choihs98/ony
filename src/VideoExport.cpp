#include <functional>
#include "VideoExport.h"
#include "Document.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <algorithm>

namespace VideoExport {

namespace {
QStringList candidatesFor(const QString& base) {
#ifdef Q_OS_WIN
    const QString exe = base + ".exe";
#else
    const QString exe = base;
#endif
    QStringList out;
    out << QDir(QCoreApplication::applicationDirPath()).filePath(exe);
    out << QDir(QCoreApplication::applicationDirPath()).filePath("ffmpeg/" + exe);
    out << QDir(QCoreApplication::applicationDirPath()).filePath("bin/" + exe);
#ifdef Q_OS_WIN
    out << "C:/ffmpeg/bin/" + exe;
    out << "C:/Program Files/ffmpeg/bin/" + exe;
    out << QDir::homePath() + "/scoop/shims/" + exe;
    out << "C:/ProgramData/chocolatey/bin/" + exe;
#else
    out << "/usr/bin/" + exe << "/usr/local/bin/" + exe << "/opt/homebrew/bin/" + exe;
#endif
    return out;
}

QString findTool(const QString& base) {
    if (base == QStringLiteral("ffmpeg")) {
        const QString cfg = configuredFfmpeg();
        if (!cfg.isEmpty() && QFile::exists(cfg)) return cfg;
    } else {
        // ffprobe 는 ffmpeg 를 지정한 폴더에서 먼저 찾는다
        const QString cfg = configuredFfmpeg();
        if (!cfg.isEmpty()) {
#ifdef Q_OS_WIN
            const QString sib = QFileInfo(cfg).absolutePath() + "/ffprobe.exe";
#else
            const QString sib = QFileInfo(cfg).absolutePath() + "/ffprobe";
#endif
            if (QFile::exists(sib)) return sib;
        }
    }
    for (const QString& p : candidatesFor(base))
        if (QFile::exists(p)) return p;
    return QStandardPaths::findExecutable(base);
}
} // namespace

QString configuredFfmpeg() { return QSettings("Ony", "Ony").value("tools/ffmpeg").toString(); }

void setConfiguredFfmpeg(const QString& path) {
    QSettings st("Ony", "Ony");
    if (path.isEmpty()) st.remove("tools/ffmpeg");
    else st.setValue("tools/ffmpeg", path);
}

QStringList searchedPaths() {
    QStringList out;
    const QString cfg = configuredFfmpeg();
    if (!cfg.isEmpty()) out << cfg + (QFile::exists(cfg) ? QStringLiteral("   (설정에 지정함)") : QStringLiteral("   (설정에 지정했지만 파일이 없음)"));
    for (const QString& p : candidatesFor(QStringLiteral("ffmpeg")))
        out << QDir::toNativeSeparators(p) + (QFile::exists(p) ? QStringLiteral("   (있음)") : QString());
    const QString inPath = QStandardPaths::findExecutable("ffmpeg");
    out << (inPath.isEmpty() ? QStringLiteral("PATH 에서 ffmpeg   (없음)") : QStringLiteral("PATH: ") + QDir::toNativeSeparators(inPath) + QStringLiteral("   (있음)"));
    return out;
}

QString findFfmpeg() { return findTool(QStringLiteral("ffmpeg")); }
QString findFfprobe() { return findTool(QStringLiteral("ffprobe")); }

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
            doc->tagColorSpace(save);   // 프로젝트 색 공간을 붙인다
            const bool ok = o.format == "jpg" ? [&] {
                QImage j = save.convertToFormat(QImage::Format_RGB32);
                doc->tagColorSpace(j);
                return j.save(path, "JPG", 95);
            }() : save.save(path);
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


// ---------------------------------------------------------------- 두 단계 내보내기
QString renderFrames(Document* doc, const QString& dir, const VideoOptions& o, const std::function<bool(int, int)>& progress) {
    QDir().mkpath(dir);
    const int n = doc->frameCount();
    const int from = std::clamp(o.from, 0, n - 1);
    const int to = std::clamp(o.to < 0 ? n - 1 : o.to, from, n - 1);
    const int total = to - from + 1;
    QColor bg = o.background;
    const bool alphaOk = o.format == "png" || o.format == "tiff" || o.format == "mov" || o.format == "webm";
    if (bg.alpha() == 0 && !alphaOk) bg = Qt::white;
    for (int f = from; f <= to; ++f) {
        if (!doc->renderFrame(f, bg, o.useCamera).save(QDir(dir).filePath(QString("frame_%1.png").arg(f - from + 1, 5, 10, QChar('0'))), "PNG"))
            return QString("프레임 %1 을 렌더링하지 못했습니다.").arg(f + 1);
        if (progress && !progress(f - from + 1, total)) return QStringLiteral("취소됨");
    }
    return {};
}

QString encodeFrames(const QString& dir, const QString& outPath, const VideoOptions& o, int fpsFallback,
                     const QString& audioPath, double audioStartSec) {
    const QString ff = o.ffmpeg.isEmpty() ? findFfmpeg() : o.ffmpeg;
    if (ff.isEmpty())
        return QStringLiteral("ffmpeg 를 찾을 수 없습니다.\nffmpeg.exe 를 ONY 실행 파일(ony.exe)과 같은 폴더에 두거나, ffmpeg 를 설치해 PATH 에 추가하세요.");
    const int fpsOut = o.fps > 0 ? o.fps : fpsFallback;
    const int crf[] = {16, 21, 27};
    const QString q = QString::number(crf[std::clamp(o.quality, 0, 2)]);
    const int sp = std::clamp(o.scalePercent, 10, 400);
    const QString scale = QString("scale=trunc(iw*%1/200)*2:trunc(ih*%1/200)*2:flags=lanczos").arg(sp);
    QStringList args{"-y", "-hide_banner", "-loglevel", "error", "-framerate", QString::number(fpsOut), "-i",
                     QDir(dir).filePath("frame_%05d.png")};
    const bool wantAudio = o.audio && o.format != "gif" && !audioPath.isEmpty() && QFile::exists(audioPath);
    if (wantAudio) args << "-ss" << QString::number(audioStartSec, 'f', 3) << "-i" << audioPath;
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
    } else {
        args << "-vf" << scale << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << q << "-pix_fmt" << "yuv420p"
             << "-movflags" << "+faststart";
        if (wantAudio) args << "-c:a" << "aac" << "-b:a" << "192k" << "-shortest";
    }
    args << outPath;
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(ff, args);
    if (!p.waitForStarted(5000)) return QString("ffmpeg 를 실행할 수 없습니다: %1").arg(ff);
    if (!p.waitForFinished(-1)) return QStringLiteral("ffmpeg 가 끝나지 않았습니다.");
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        const QString log = QString::fromLocal8Bit(p.readAll()).trimmed();
        return QString("ffmpeg 오류 (코드 %1):\n%2").arg(p.exitCode()).arg(log.right(600));
    }
    return {};
}

// ---------------------------------------------------------------- 동영상 읽기
VideoInfo probe(const QString& path, const QString& hint) {
    VideoInfo info;
    QString probeExe = hint.isEmpty() ? findFfprobe() : hint;
    if (probeExe.isEmpty() || !QFile::exists(probeExe)) {
        // ffprobe 가 없으면 ffmpeg 로 정보를 뽑는다
        const QString ff = findFfmpeg();
        if (ff.isEmpty()) return info;
        QProcess p;
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(ff, {"-hide_banner", "-i", path});
        p.waitForFinished(15000);
        const QString log = QString::fromLocal8Bit(p.readAll());
        static const QRegularExpression reSize("([0-9]{2,5})x([0-9]{2,5})");
        static const QRegularExpression reFps("([0-9.]+) fps");
        static const QRegularExpression reDur("Duration: ([0-9]+):([0-9]+):([0-9.]+)");
        const auto ms = reSize.match(log);
        if (ms.hasMatch()) info.size = QSize(ms.captured(1).toInt(), ms.captured(2).toInt());
        const auto mf = reFps.match(log);
        if (mf.hasMatch()) info.fps = mf.captured(1).toDouble();
        const auto md = reDur.match(log);
        if (md.hasMatch()) info.duration = md.captured(1).toDouble() * 3600 + md.captured(2).toDouble() * 60 + md.captured(3).toDouble();
        info.hasAudio = log.contains("Audio:");
        info.ok = info.size.isValid() && info.size.width() > 0;
        return info;
    }
    QProcess p;
    p.start(probeExe, {"-v", "error", "-show_entries", "stream=width,height,avg_frame_rate,codec_type",
                       "-show_entries", "format=duration", "-of", "default=noprint_wrappers=1", path});
    p.waitForFinished(15000);
    const QStringList lines = QString::fromLocal8Bit(p.readAllStandardOutput()).split('\n');
    int w = 0, h = 0;
    for (const QString& ln : lines) {
        const int eq = ln.indexOf('=');
        if (eq < 0) continue;
        const QString k = ln.left(eq), v = ln.mid(eq + 1).trimmed();
        if (k == "width" && w == 0) w = v.toInt();
        else if (k == "height" && h == 0) h = v.toInt();
        else if (k == "duration") info.duration = v.toDouble();
        else if (k == "codec_type" && v == "audio") info.hasAudio = true;
        else if (k == "avg_frame_rate" && info.fps <= 0) {
            const QStringList fr = v.split('/');
            if (fr.size() == 2 && fr[1].toDouble() > 0) info.fps = fr[0].toDouble() / fr[1].toDouble();
        }
    }
    info.size = QSize(w, h);
    info.ok = w > 0 && h > 0;
    return info;
}

QString extractFrames(const QString& path, const QString& dir, const ImportOptions& o, QStringList* outFiles,
                      const std::function<bool(int, int)>& progress) {
    const QString ff = o.ffmpeg.isEmpty() ? findFfmpeg() : o.ffmpeg;
    if (ff.isEmpty())
        return QStringLiteral("ffmpeg 를 찾을 수 없습니다.\nffmpeg.exe 를 ONY 실행 파일(ony.exe)과 같은 폴더에 두거나, ffmpeg 를 설치해 PATH 에 추가하세요.");
    if (!QFile::exists(path)) return QStringLiteral("동영상 파일이 없습니다.");
    QDir().mkpath(dir);
    QStringList args{"-y", "-hide_banner", "-loglevel", "error"};
    if (o.startSec > 0) args << "-ss" << QString::number(o.startSec, 'f', 3);
    args << "-i" << path;
    if (o.durationSec > 0) args << "-t" << QString::number(o.durationSec, 'f', 3);
    if (o.fps > 0) args << "-vf" << QString("fps=%1").arg(o.fps, 0, 'f', 4);
    args << "-frames:v" << QString::number(std::clamp(o.maxFrames, 1, 20000));
    args << QDir(dir).filePath("in_%05d.png");
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(ff, args);
    if (!p.waitForStarted(5000)) return QString("ffmpeg 를 실행할 수 없습니다: %1").arg(ff);
    while (!p.waitForFinished(150)) {
        if (progress && !progress(QDir(dir).entryList({"in_*.png"}, QDir::Files).size(), std::max(1, o.maxFrames))) {
            p.kill();
            p.waitForFinished(2000);
            return QStringLiteral("취소됨");
        }
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        const QString log = QString::fromLocal8Bit(p.readAll()).trimmed();
        return QString("ffmpeg 오류 (코드 %1):\n%2").arg(p.exitCode()).arg(log.right(600));
    }
    QStringList files = QDir(dir).entryList({"in_*.png"}, QDir::Files, QDir::Name);
    for (QString& s : files) s = QDir(dir).filePath(s);
    if (files.isEmpty()) return QStringLiteral("동영상에서 프레임을 얻지 못했습니다.");
    if (outFiles) *outFiles = files;
    return {};
}

QVector<float> audioPeaks(const QString& audioPath, int buckets, const QString& hint) {
    QVector<float> peaks;
    if (audioPath.isEmpty() || !QFile::exists(audioPath) || buckets < 1) return peaks;
    const QString ff = hint.isEmpty() ? findFfmpeg() : hint;
    if (ff.isEmpty()) return peaks;
    QProcess p;
    p.start(ff, {"-v", "error", "-i", audioPath, "-ac", "1", "-ar", "8000", "-f", "s16le", "-"});
    if (!p.waitForStarted(5000)) return peaks;
    if (!p.waitForFinished(60000)) {
        p.kill();
        p.waitForFinished(2000);
        return peaks;
    }
    const QByteArray pcm = p.readAllStandardOutput();
    const int n = pcm.size() / 2;
    if (n <= 0) return peaks;
    const qint16* s = reinterpret_cast<const qint16*>(pcm.constData());
    peaks.resize(buckets);
    for (int i = 0; i < buckets; ++i) {
        const int a = qint64(i) * n / buckets, b = qint64(i + 1) * n / buckets;
        int mx = 0;
        for (int k = a; k < b && k < n; ++k) mx = std::max(mx, std::abs(int(s[k])));
        peaks[i] = float(mx) / 32768.0f;
    }
    return peaks;
}

} // namespace VideoExport
