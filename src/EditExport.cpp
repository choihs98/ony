#include "EditExport.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QUrl>
#include <algorithm>
#include "Document.h"

namespace EditExport {

QString timecode(int frame, int fps) {
    fps = std::max(1, fps);
    if (frame < 0) frame = 0;
    const int f = frame % fps;
    const int totalSec = frame / fps;
    const int sec = totalSec % 60;
    const int min = (totalSec / 60) % 60;
    const int hour = totalSec / 3600;
    return QString("%1:%2:%3:%4")
        .arg(hour, 2, 10, QChar('0'))
        .arg(min, 2, 10, QChar('0'))
        .arg(sec, 2, 10, QChar('0'))
        .arg(f, 2, 10, QChar('0'));
}

QVector<Cut> cutsOf(const Document* doc) {
    QVector<Cut> out;
    if (!doc || doc->frameCount() < 1) return out;
    const int n = doc->frameCount();
    // 북마크가 컷의 시작점이다. 0 번은 늘 시작점으로 둔다.
    QVector<int> starts;
    starts.push_back(0);
    for (int b : doc->bookmarks())
        if (b > 0 && b < n) starts.push_back(b);
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());

    for (int i = 0; i < starts.size(); ++i) {
        Cut c;
        c.from = starts[i];
        c.to = (i + 1 < starts.size()) ? starts[i + 1] - 1 : n - 1;
        if (c.to < c.from) continue;
        const QString note = doc->note(c.from).trimmed();
        c.name = note.isEmpty() ? QString("컷 %1").arg(out.size() + 1) : note;
        out.push_back(c);
    }
    return out;
}

QString buildEdl(const Document* doc, const QString& title, const QString& reelName) {
    if (!doc) return {};
    const int fps = std::max(1, doc->fps());
    const QVector<Cut> cuts = cutsOf(doc);
    QString s;
    QTextStream t(&s);
    t << "TITLE: " << (title.isEmpty() ? QStringLiteral("ONY") : title) << "\n";
    // 24 가 아닌 프레임 속도는 NON-DROP FRAME 으로 적는다
    t << "FCM: NON-DROP FRAME\n";
    // 릴 이름은 8글자까지 쓰는 편집기가 많다
    QString reel = reelName.isEmpty() ? QStringLiteral("ONY") : reelName;
    reel.remove(QRegularExpression("[^A-Za-z0-9_]"));
    if (reel.isEmpty()) reel = QStringLiteral("ONY");
    reel = reel.left(8).toUpper();

    int rec = 0;
    for (int i = 0; i < cuts.size(); ++i) {
        const Cut& c = cuts[i];
        t << QString("%1  %2 V     C        ").arg(i + 1, 3, 10, QChar('0')).arg(reel, -8);
        t << timecode(c.from, fps) << " " << timecode(c.to + 1, fps) << " ";
        t << timecode(rec, fps) << " " << timecode(rec + c.frames(), fps) << "\n";
        t << "* FROM CLIP NAME: " << c.name << "\n";
        rec += c.frames();
    }
    if (cuts.isEmpty()) t << "* 컷이 없습니다\n";
    t.flush();
    return s;
}

QString buildFcpXml(const Document* doc, const QString& title, const QString& mediaPath) {
    if (!doc) return {};
    const int fps = std::max(1, doc->fps());
    const QVector<Cut> cuts = cutsOf(doc);
    const int total = doc->frameCount();
    const QString name = title.isEmpty() ? QStringLiteral("ONY") : title;
    const QString fileName = mediaPath.isEmpty() ? QStringLiteral("ony.mov") : QFileInfo(mediaPath).fileName();
    const QString url = mediaPath.isEmpty() ? QString() : QUrl::fromLocalFile(mediaPath).toString();

    auto esc = [](const QString& v) { return v.toHtmlEscaped(); };

    QString s;
    QTextStream t(&s);
    t << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    t << "<!DOCTYPE xmeml>\n<xmeml version=\"5\">\n";
    t << "  <sequence>\n";
    t << "    <name>" << esc(name) << "</name>\n";
    t << "    <duration>" << total << "</duration>\n";
    t << "    <rate><timebase>" << fps << "</timebase><ntsc>FALSE</ntsc></rate>\n";
    t << "    <media>\n      <video>\n";
    t << "        <format><samplecharacteristics>"
      << "<width>" << doc->size().width() << "</width>"
      << "<height>" << doc->size().height() << "</height>"
      << "<rate><timebase>" << fps << "</timebase><ntsc>FALSE</ntsc></rate>"
      << "</samplecharacteristics></format>\n";
    t << "        <track>\n";
    int rec = 0;
    for (int i = 0; i < cuts.size(); ++i) {
        const Cut& c = cuts[i];
        t << "          <clipitem id=\"clip" << (i + 1) << "\">\n";
        t << "            <name>" << esc(c.name) << "</name>\n";
        t << "            <duration>" << c.frames() << "</duration>\n";
        t << "            <rate><timebase>" << fps << "</timebase><ntsc>FALSE</ntsc></rate>\n";
        t << "            <start>" << rec << "</start>\n";
        t << "            <end>" << (rec + c.frames()) << "</end>\n";
        t << "            <in>" << c.from << "</in>\n";
        t << "            <out>" << (c.to + 1) << "</out>\n";
        t << "            <file id=\"file1\"";
        if (i == 0) {
            t << ">\n";
            t << "              <name>" << esc(fileName) << "</name>\n";
            if (!url.isEmpty()) t << "              <pathurl>" << esc(url) << "</pathurl>\n";
            t << "              <duration>" << total << "</duration>\n";
            t << "              <rate><timebase>" << fps << "</timebase><ntsc>FALSE</ntsc></rate>\n";
            t << "            </file>\n";
        } else {
            t << "/>\n";
        }
        t << "          </clipitem>\n";
        rec += c.frames();
    }
    t << "        </track>\n      </video>\n    </media>\n";
    // 북마크는 마커로도 남긴다
    for (const Cut& c : cuts) {
        t << "    <marker><name>" << esc(c.name) << "</name><in>" << c.from << "</in><out>-1</out></marker>\n";
    }
    t << "  </sequence>\n</xmeml>\n";
    t.flush();
    return s;
}

static QString writeText(const QString& path, const QString& text) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return QString("파일을 쓰지 못했습니다: %1").arg(path);
    const QByteArray data = text.toUtf8();
    if (f.write(data) != data.size()) return QStringLiteral("파일을 끝까지 쓰지 못했습니다.");
    return {};
}

QString writeEdl(const Document* doc, const QString& path, const QString& title) {
    if (!doc) return QStringLiteral("프로젝트가 없습니다.");
    return writeText(path, buildEdl(doc, title, title));
}

QString writeFcpXml(const Document* doc, const QString& path, const QString& title, const QString& mediaPath) {
    if (!doc) return QStringLiteral("프로젝트가 없습니다.");
    return writeText(path, buildFcpXml(doc, title, mediaPath));
}

}   // namespace EditExport
