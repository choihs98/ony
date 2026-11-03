#include "Collab.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSysInfo>
#include <QTextStream>
#include <algorithm>

namespace Collab {

QString userName() {
    const QString s = QSettings("Ony", "Ony").value("collab/user").toString().trimmed();
    if (!s.isEmpty()) return s;
    QString env = qEnvironmentVariable("USERNAME");
    if (env.isEmpty()) env = qEnvironmentVariable("USER");
    return env.isEmpty() ? QStringLiteral("이름없음") : env;
}

void setUserName(const QString& name) {
    QSettings("Ony", "Ony").setValue("collab/user", name.trimmed());
}

QString machineName() {
    const QString h = QSysInfo::machineHostName();
    return h.isEmpty() ? QStringLiteral("알 수 없는 컴퓨터") : h;
}

int staleMinutes() { return QSettings("Ony", "Ony").value("collab/staleMinutes", 120).toInt(); }
// 잠금을 "오래됨"으로 볼 기준 시간(분)을 저장한다
void setStaleMinutes(int m) { QSettings("Ony", "Ony").setValue("collab/staleMinutes", std::max(5, m)); }

QString lockPath(const QString& projectPath) { return projectPath + ".lock"; }
QString historyPath(const QString& projectPath) { return projectPath + ".history"; }

LockInfo checkLock(const QString& projectPath) {
    LockInfo info;
    if (projectPath.isEmpty()) return info;
    QFile f(lockPath(projectPath));
    if (!f.exists() || !f.open(QIODevice::ReadOnly | QIODevice::Text)) return info;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return info;
    const QJsonObject o = doc.object();
    info.locked = true;
    info.user = o.value("user").toString();
    info.machine = o.value("machine").toString();
    info.since = QDateTime::fromString(o.value("since").toString(), Qt::ISODate);
    const QDateTime touched = QDateTime::fromString(o.value("touched").toString(), Qt::ISODate);
    const QDateTime last = touched.isValid() ? touched : info.since;
    info.mine = info.user == userName() && info.machine == machineName();
    info.stale = last.isValid() && last.secsTo(QDateTime::currentDateTime()) > staleMinutes() * 60;
    return info;
}

static bool writeLock(const QString& projectPath, const QDateTime& since) {
    QFile f(lockPath(projectPath));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QJsonObject o;
    o["user"] = userName();
    o["machine"] = machineName();
    o["since"] = since.toString(Qt::ISODate);
    o["touched"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}

bool acquireLock(const QString& projectPath, LockInfo* existing) {
    if (projectPath.isEmpty()) return false;
    const LockInfo cur = checkLock(projectPath);
    if (cur.locked && !cur.mine && !cur.stale) {
        if (existing) *existing = cur;
        return false;
    }
    if (existing) *existing = cur;
    return writeLock(projectPath, cur.mine && cur.since.isValid() ? cur.since : QDateTime::currentDateTime());
}

void touchLock(const QString& projectPath) {
    const LockInfo cur = checkLock(projectPath);
    if (!cur.locked || !cur.mine) return;
    writeLock(projectPath, cur.since.isValid() ? cur.since : QDateTime::currentDateTime());
}

bool releaseLock(const QString& projectPath, bool force) {
    if (projectPath.isEmpty()) return false;
    const LockInfo cur = checkLock(projectPath);
    if (!cur.locked) return true;
    if (!cur.mine && !force) return false;
    return QFile::remove(lockPath(projectPath));
}

QVector<HistoryEntry> readHistory(const QString& projectPath) {
    QVector<HistoryEntry> out;
    if (projectPath.isEmpty()) return out;
    QFile f(historyPath(projectPath));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    for (const QJsonValue& v : doc.array()) {
        const QJsonObject o = v.toObject();
        HistoryEntry e;
        e.when = QDateTime::fromString(o.value("when").toString(), Qt::ISODate);
        e.user = o.value("user").toString();
        e.machine = o.value("machine").toString();
        e.note = o.value("note").toString();
        e.bytes = qint64(o.value("bytes").toDouble());
        out.push_back(e);
    }
    return out;
}

void appendHistory(const QString& projectPath, const QString& note, qint64 bytes) {
    if (projectPath.isEmpty()) return;
    QVector<HistoryEntry> list = readHistory(projectPath);
    HistoryEntry e;
    e.when = QDateTime::currentDateTime();
    e.user = userName();
    e.machine = machineName();
    e.note = note;
    e.bytes = bytes;
    list.push_back(e);
    while (list.size() > 500) list.remove(0);
    QJsonArray arr;
    for (const HistoryEntry& h : list) {
        QJsonObject o;
        o["when"] = h.when.toString(Qt::ISODate);
        o["user"] = h.user;
        o["machine"] = h.machine;
        o["note"] = h.note;
        o["bytes"] = double(h.bytes);
        arr.append(o);
    }
    QFile f(historyPath(projectPath));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

} // namespace Collab
