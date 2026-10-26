#pragma once
#include <QAction>
#include <QKeySequence>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>
#include <functional>

class QWidget;

// 모든 단축키를 한 곳에서 관리한다. 메뉴 항목과 도구 단축키가 모두 여기에 등록되고,
// 사용자가 바꾼 키는 설정 파일에 저장된다.
class Shortcuts : public QObject {
    Q_OBJECT
public:
    struct Cmd {
        QString id, category, label;
        QKeySequence def;
        QPointer<QAction> action;
    };
    static Shortcuts& instance();

    // 새 명령(액션)을 만들어 owner 에 붙인다.
    QAction* add(QWidget* owner, const QString& category, const QString& label, const QKeySequence& def, std::function<void()> fn);
    // 이미 만들어진 (메뉴) 액션을 등록한다. 기본 키는 액션에 설정된 값.
    void adopt(QAction* a, const QString& category);

    const QVector<Cmd>& commands() const { return cmds_; }
    QKeySequence current(const QString& id) const;
    // 키를 바꾼다. 같은 키를 쓰던 다른 명령이 있으면 그 명령의 키를 해제하고 그 이름을 돌려준다.
    QString set(const QString& id, const QKeySequence& ks);
    void reset(const QString& id);
    void resetAll();

signals:
    void changed();

private:
    Shortcuts();
    void registerCmd(QAction* a, const QString& category, const QString& label, const QKeySequence& def);
    void applyTo(Cmd& c);
    void persist(const QString& id);
    QVector<Cmd> cmds_;
    QMap<QString, QString> overrides_; // id -> 키 문자열 (빈 문자열 = 해제)
};

void showShortcutDialog(QWidget* parent);
