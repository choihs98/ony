#pragma once
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

// 공유 폴더로 여럿이 돌아가며 작업하기.
// 서버 없이, 프로젝트 파일 옆에 두는 작은 파일 두 개로 돌아간다.
//   <파일>.ony.lock      지금 누가 열고 있는지
//   <파일>.ony.history   누가 언제 무엇을 저장했는지
namespace Collab {

struct LockInfo {
    bool locked = false;
    QString user;
    QString machine;
    QDateTime since;
    bool mine = false;       // 내가 건 잠금인지
    bool stale = false;      // 오래돼서 버려진 것으로 보이는지
};

struct HistoryEntry {
    QDateTime when;
    QString user;
    QString machine;
    QString note;
    qint64 bytes = 0;
};

// 지금 사용자 이름 (설정 > 비어 있으면 로그인 이름)
QString userName();
void setUserName(const QString& name);
QString machineName();

QString lockPath(const QString& projectPath);
QString historyPath(const QString& projectPath);

LockInfo checkLock(const QString& projectPath);
// 잠금을 건다. 이미 남이 걸었으면 false 와 그 정보를 돌려준다.
bool acquireLock(const QString& projectPath, LockInfo* existing = nullptr);
// 내 잠금을 푼다 (남의 잠금은 force 일 때만)
bool releaseLock(const QString& projectPath, bool force = false);
// 잠금이 살아 있다고 알린다 (시간 갱신)
void touchLock(const QString& projectPath);

QVector<HistoryEntry> readHistory(const QString& projectPath);
void appendHistory(const QString& projectPath, const QString& note, qint64 bytes);

// 잠금이 몇 분 지나면 버려진 것으로 볼지
int staleMinutes();
void setStaleMinutes(int m);

}
