#pragma once
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

// ONY 플러그인: 폴더 하나에 ony-plugin.json + 스크립트 파일을 넣으면 메뉴에 올라온다.
// (C++ 로 만든 DLL 이 아니라 스크립트라, 윈도우/리눅스 구분 없이 그대로 돈다.)
namespace Plugins {

struct Command {
    QString label;       // 메뉴에 보일 이름
    QString script;      // 실행할 스크립트 본문
    QString shortcut;    // 선택: 단축키
    QString ask;         // 선택: 실행 전에 숫자를 물어볼 변수 이름 ("이름:설명:기본값")
};

struct Plugin {
    QString id;
    QString name;
    QString version;
    QString author;
    QString description;
    QString dir;         // 플러그인 폴더
    QVector<Command> commands;
    bool enabled = true;
    QString error;       // 읽다가 생긴 문제
};

// 플러그인 폴더 (없으면 만든다)
QString pluginsDir();
// 폴더를 훑어 플러그인을 읽는다
QVector<Plugin> scan();
// 켜짐/꺼짐 설정 저장·적용
bool isEnabled(const QString& id);
void setEnabled(const QString& id, bool on);
// 예제 플러그인을 만들어 둔다 (처음 한 번)
void installSample();
// zip 이 아니라 폴더째 들어오므로, 설치는 폴더 복사. 성공하면 빈 문자열.
QString installFromFolder(const QString& srcDir);
QString removePlugin(const QString& id);

}
