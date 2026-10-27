#include "Plugins.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSettings>
#include <QStandardPaths>

namespace Plugins {

QString pluginsDir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString dir = QDir(base).filePath("plugins");
    QDir().mkpath(dir);
    return dir;
}

bool isEnabled(const QString& id) {
    return QSettings("Ony", "Ony").value("plugins/enabled/" + id, true).toBool();
}

void setEnabled(const QString& id, bool on) {
    QSettings("Ony", "Ony").setValue("plugins/enabled/" + id, on);
}

static Plugin readOne(const QString& dir) {
    Plugin p;
    p.dir = dir;
    p.id = QFileInfo(dir).fileName();
    p.name = p.id;
    QFile f(QDir(dir).filePath("ony-plugin.json"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        p.error = QStringLiteral("ony-plugin.json 을 열 수 없습니다");
        return p;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        p.error = QString("ony-plugin.json 을 읽지 못했습니다 (%1)").arg(err.errorString());
        return p;
    }
    const QJsonObject o = doc.object();
    p.name = o.value("name").toString(p.id);
    p.version = o.value("version").toString();
    p.author = o.value("author").toString();
    p.description = o.value("description").toString();
    if (!o.value("id").toString().isEmpty()) p.id = o.value("id").toString();
    for (const QJsonValue& cv : o.value("commands").toArray()) {
        const QJsonObject co = cv.toObject();
        Command c;
        c.label = co.value("label").toString();
        c.shortcut = co.value("shortcut").toString();
        c.ask = co.value("ask").toString();
        const QString file = co.value("script").toString();
        if (!file.isEmpty()) {
            QFile sf(QDir(dir).filePath(file));
            if (sf.open(QIODevice::ReadOnly | QIODevice::Text)) c.script = QString::fromUtf8(sf.readAll());
            else p.error += QString("스크립트 파일을 못 읽었습니다: %1\n").arg(file);
        } else {
            c.script = co.value("run").toString();
        }
        if (!c.label.isEmpty() && !c.script.isEmpty()) p.commands.push_back(c);
    }
    if (p.commands.isEmpty() && p.error.isEmpty()) p.error = QStringLiteral("실행할 명령이 없습니다");
    p.enabled = isEnabled(p.id);
    return p;
}

QVector<Plugin> scan() {
    QVector<Plugin> out;
    const QDir root(pluginsDir());
    for (const QFileInfo& fi : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!QFile::exists(QDir(fi.absoluteFilePath()).filePath("ony-plugin.json"))) continue;
        out.push_back(readOne(fi.absoluteFilePath()));
    }
    return out;
}

void installSample() {
    // 예제는 늘 최신으로 덮어쓴다 (고쳐 쓸 때는 폴더를 복사해서 쓰라고 안내한다)
    const QString dir = QDir(pluginsDir()).filePath("ony-sample");
    QDir().mkpath(dir);
    QFile j(QDir(dir).filePath("ony-plugin.json"));
    if (j.open(QIODevice::WriteOnly | QIODevice::Text)) {
        j.write(R"ONY({
  "id": "ony-sample",
  "name": "예제 플러그인",
  "version": "1.0",
  "author": "ONY",
  "description": "플러그인을 어떻게 만드는지 보여 주는 예제입니다. 이 폴더를 복사해서 고쳐 쓰세요.",
  "commands": [
    { "label": "바둑판 배경 그리기", "script": "checker.ony-script" },
    { "label": "현재 레이어 뒤집고 반전", "run": "flip.h\nfx.invert" },
    { "label": "프레임마다 번호 찍기", "script": "numbers.ony-script" },
    { "label": "흐리게 (세기 묻기)", "ask": "n:얼마나 흐리게 할까요? (1~16):4", "run": "fx.blur $n" }
  ]
}
)ONY");
    }
    QFile c(QDir(dir).filePath("checker.ony-script"));
    if (c.open(QIODevice::WriteOnly | QIODevice::Text)) {
        c.write(R"ONY(# 바둑판 배경을 그린다
get w width
get h height
set cell = 40
set rows = int($h / $cell) + 1
set cols = int($w / $cell) + 1
layer.new 바둑판
for r = 0 to $rows - 1
  for c = 0 to $cols - 1
    set x = $c * $cell
    set y = $r * $cell
    if ($r + $c) % 2 == 0
      rect $x $y $cell $cell #dde6ee
    else
      rect $x $y $cell $cell #ffffff
    end
  end
end
msg 바둑판 배경을 그렸습니다
)ONY");
    }
    QFile n(QDir(dir).filePath("numbers.ony-script"));
    if (n.open(QIODevice::WriteOnly | QIODevice::Text)) {
        n.write(R"ONY(# 프레임마다 작은 네모를 찍어 순서를 알아보기 쉽게 한다
get total frames
layer.new 번호
for i = 1 to $total
  frame $i
  key.new
  set bw = $i * 6
  rect 8 8 $bw 10 #ff5533
end
frame 1
msg 프레임 표시를 넣었습니다
)ONY");
    }
    QFile r(QDir(dir).filePath("읽어보세요.txt"));
    if (r.open(QIODevice::WriteOnly | QIODevice::Text)) {
        r.write(QString::fromUtf8(
                    "ONY 플러그인 만드는 법\n"
                    "\n"
                    "1. 이 폴더(ony-sample)를 통째로 복사하고 이름을 바꿉니다.\n"
                    "2. ony-plugin.json 의 id 와 name 을 바꿉니다. id 는 폴더마다 달라야 합니다.\n"
                    "3. commands 에 메뉴에 올릴 명령을 적습니다.\n"
                    "   - \"run\": 스크립트를 바로 적기\n"
                    "   - \"script\": 같은 폴더의 스크립트 파일 이름\n"
                    "   - \"shortcut\": 단축키 (예: \"Ctrl+Shift+1\")\n"
                    "   - \"ask\": 실행 전에 숫자를 물어볼 때. \"변수이름:물어볼 말:기본값\"\n"
                    "4. ONY 를 다시 켜거나 [플러그인] > [다시 읽기] 를 누릅니다.\n"
                    "\n"
                    "주의: 이 ony-sample 폴더는 ONY 를 켤 때마다 예제 내용으로 덮어씁니다.\n"
                    "      고쳐 쓸 때는 꼭 다른 이름으로 복사한 뒤 고치세요.\n"
                    "\n"
                    "쓸 수 있는 명령은 스크립트 콘솔의 [도움말] 버튼에서 모두 볼 수 있습니다.\n")
                    .toUtf8());
    }
}

QString installFromFolder(const QString& srcDir) {
    QDir src(srcDir);
    if (!src.exists()) return QStringLiteral("폴더가 없습니다.");
    if (!QFile::exists(src.filePath("ony-plugin.json")))
        return QStringLiteral("그 폴더에 ony-plugin.json 이 없습니다. 플러그인 폴더를 고르세요.");
    const QString name = src.dirName();
    const QString dst = QDir(pluginsDir()).filePath(name);
    if (QFile::exists(QDir(dst).filePath("ony-plugin.json")))
        return QString("이미 같은 이름의 플러그인이 있습니다: %1").arg(name);
    QDir().mkpath(dst);
    for (const QFileInfo& fi : src.entryInfoList(QDir::Files)) {
        if (!QFile::copy(fi.absoluteFilePath(), QDir(dst).filePath(fi.fileName())))
            return QString("파일을 복사하지 못했습니다: %1").arg(fi.fileName());
    }
    return {};
}

QString removePlugin(const QString& id) {
    const QString dir = QDir(pluginsDir()).filePath(id);
    if (!QFile::exists(QDir(dir).filePath("ony-plugin.json"))) return QStringLiteral("그 플러그인이 없습니다.");
    QDir d(dir);
    return d.removeRecursively() ? QString() : QStringLiteral("폴더를 지우지 못했습니다.");
}

} // namespace Plugins
