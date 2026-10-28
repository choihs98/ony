#pragma once
#include <QString>
#include <QStringList>
#include <QVector>

class Document;

// 편집 프로그램으로 타임라인을 넘기는 파일을 만든다.
// 북마크로 구간을 나눠 각 구간을 하나의 컷으로 본다 (북마크가 없으면 전체가 한 컷).
namespace EditExport {

struct Cut {
    int from = 0;      // 시작 프레임 (0부터)
    int to = 0;        // 끝 프레임 (포함)
    QString name;      // 컷 이름 (메모가 있으면 메모, 없으면 "컷 N")
    int frames() const { return to - from + 1; }
};

// 북마크와 프레임 메모로 컷 목록을 만든다
QVector<Cut> cutsOf(const Document* doc);

// 프레임 번호를 HH:MM:SS:FF 타임코드로
QString timecode(int frame, int fps);

// CMX3600 EDL 문자열
QString buildEdl(const Document* doc, const QString& title, const QString& reelName);
// Final Cut Pro 7 식 XML (프리미어도 읽는다)
QString buildFcpXml(const Document* doc, const QString& title, const QString& mediaPath);

// 파일로 쓴다. 실패하면 사유를 돌려주고, 성공이면 빈 문자열.
QString writeEdl(const Document* doc, const QString& path, const QString& title);
QString writeFcpXml(const Document* doc, const QString& path, const QString& title, const QString& mediaPath);

}   // namespace EditExport
