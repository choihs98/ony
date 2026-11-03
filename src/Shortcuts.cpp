#include <functional>
#include <algorithm>
#include "Shortcuts.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

static QString cleanLabel(QString s) {
    s.remove('&');
    return s;
}

// 생성자: 저장된 단축키 재정의 값을 설정에서 불러온다
Shortcuts::Shortcuts() {
    QSettings st("Ony", "Ony");
    st.beginGroup("shortcuts");
    for (const QString& k : st.childKeys()) {
        QString id = k;
        id.replace('|', '/');
        overrides_[id] = st.value(k).toString();
    }
}

// 단축키 관리자(싱글턴 인스턴스)를 가져온다
Shortcuts& Shortcuts::instance() {
    static Shortcuts s;
    return s;
}

// 현재 단축키 값을 실제 액션(QAction)에 적용한다
void Shortcuts::applyTo(Cmd& c) {
    if (!c.action) return;
    c.action->setShortcut(current(c.id));
}

// 명령 id 에 대해 지금 쓰이는 단축키를 가져온다 (재정의가 있으면 그것, 없으면 기본값)
QKeySequence Shortcuts::current(const QString& id) const {
    if (overrides_.contains(id)) return QKeySequence::fromString(overrides_[id]);
    for (const Cmd& c : cmds_)
        if (c.id == id) return c.def;
    return QKeySequence();
}

// 명령을 단축키 목록에 등록한다 (이미 있으면 액션만 교체한다)
void Shortcuts::registerCmd(QAction* a, const QString& category, const QString& label, const QKeySequence& def) {
    const QString id = category + "/" + label;
    for (Cmd& c : cmds_) {
        if (c.id == id) { // 메뉴가 다시 만들어진 경우: 액션만 교체
            c.action = a;
            c.def = def;
            applyTo(c);
            return;
        }
    }
    cmds_.push_back({id, category, label, def, a});
    applyTo(cmds_.last());
}

// 새 액션을 만들고 단축키와 실행 함수를 연결해서 등록한다
QAction* Shortcuts::add(QWidget* owner, const QString& category, const QString& label, const QKeySequence& def, std::function<void()> fn) {
    auto* a = new QAction(label, owner);
    a->setShortcutContext(Qt::WindowShortcut);
    owner->addAction(a);
    QObject::connect(a, &QAction::triggered, owner, [fn] { fn(); });
    registerCmd(a, category, label, def);
    return a;
}

// 이미 만들어진 액션을 단축키 목록에 등록한다
void Shortcuts::adopt(QAction* a, const QString& category) {
    registerCmd(a, category, cleanLabel(a->text()), a->shortcut());
}

// 단축키 재정의 값을 설정 파일에 저장한다
void Shortcuts::persist(const QString& id) {
    QSettings st("Ony", "Ony");
    st.beginGroup("shortcuts");
    QString k = id;
    k.replace('/', '|');
    if (overrides_.contains(id)) st.setValue(k, overrides_[id]);
    else st.remove(k);
}

// 명령의 단축키를 바꾼다 (같은 키를 쓰던 다른 명령이 있으면 그 쪽은 해제된다)
QString Shortcuts::set(const QString& id, const QKeySequence& ks) {
    QString lost;
    if (!ks.isEmpty()) {
        for (Cmd& o : cmds_) {
            if (o.id != id && current(o.id) == ks) {
                overrides_[o.id] = QString();
                persist(o.id);
                applyTo(o);
                lost = o.label;
            }
        }
    }
    Cmd* me = nullptr;
    for (Cmd& c : cmds_) if (c.id == id) me = &c;
    if (!me) return lost;
    if (ks == me->def) overrides_.remove(id);
    else overrides_[id] = ks.toString(QKeySequence::PortableText);
    persist(id);
    applyTo(*me);
    emit changed();
    return lost;
}

// 특정 명령의 단축키를 기본값으로 되돌린다
void Shortcuts::reset(const QString& id) {
    for (const Cmd& c : cmds_)
        if (c.id == id) {
            set(id, c.def);
            return;
        }
}

// 모든 단축키를 기본값으로 되돌린다
void Shortcuts::resetAll() {
    const auto ids = overrides_.keys();
    overrides_.clear();
    for (const QString& id : ids) persist(id);
    for (Cmd& c : cmds_) applyTo(c);
    emit changed();
}

// ---------------------------------------------------------------- 설정 창
QString Shortcuts::saveProfile(const QString& path) const {
    QJsonObject keys;
    for (const Cmd& c : cmds_) {
        const QKeySequence ks = current(c.id);
        if (ks != c.def) keys[c.id] = ks.toString(QKeySequence::PortableText);
    }
    QJsonObject root;
    root["type"] = "ony-shortcuts";
    root["version"] = 1;
    root["keys"] = keys;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return QStringLiteral("파일을 만들 수 없습니다.");
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return {};
}

// 단축키 설정을 파일에서 불러온다
QString Shortcuts::loadProfile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QStringLiteral("파일을 열 수 없습니다.");
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return QStringLiteral("단축키 파일이 아닙니다.");
    const QJsonObject root = doc.object();
    if (root.value("type").toString() != QStringLiteral("ony-shortcuts")) return QStringLiteral("ONY 단축키 파일이 아닙니다.");
    resetAll();
    const QJsonObject keys = root.value("keys").toObject();
    int n = 0;
    for (auto it = keys.begin(); it != keys.end(); ++it) {
        const QString ks = it.value().toString();
        for (const Cmd& c : cmds_)
            if (c.id == it.key()) {
                set(c.id, QKeySequence(ks, QKeySequence::PortableText));
                ++n;
                break;
            }
    }
    emit changed();
    return {};
}

void showShortcutDialog(QWidget* parent) {
    Shortcuts& S = Shortcuts::instance();
    QDialog dlg(parent);
    dlg.setWindowTitle("단축키 설정");
    dlg.resize(620, 560);
    auto* lay = new QVBoxLayout(&dlg);
    auto* search = new QLineEdit;
    search->setPlaceholderText("명령 검색...");
    search->setClearButtonEnabled(true);
    lay->addWidget(search);
    auto* table = new QTableWidget;
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({"분류", "명령", "단축키"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    table->setColumnWidth(2, 190);
    table->verticalHeader()->hide();
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(table, 1);
    auto* note = new QLabel("칸을 클릭하고 원하는 키 조합을 누르세요. 같은 키를 쓰던 다른 명령은 자동으로 해제됩니다.");
    note->setWordWrap(true);
    lay->addWidget(note);

    QVector<QKeySequenceEdit*> edits;
    auto cmds = S.commands();
    std::stable_sort(cmds.begin(), cmds.end(), [](const auto& a, const auto& b) { return a.category < b.category; });
    table->setRowCount(cmds.size());
    for (int r = 0; r < cmds.size(); ++r) {
        auto* ci = new QTableWidgetItem(cmds[r].category);
        if (r == 0 || cmds[r - 1].category != cmds[r].category) {
            QFont f = ci->font(); f.setBold(true); ci->setFont(f);
            ci->setBackground(QColor(128, 128, 128, 40));
        }
        table->setItem(r, 0, ci);
        table->setItem(r, 1, new QTableWidgetItem(cmds[r].label));
        auto* ed = new QKeySequenceEdit(S.current(cmds[r].id));
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        ed->setMaximumSequenceLength(1);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
        ed->setClearButtonEnabled(true);
#endif
        table->setCellWidget(r, 2, ed);
        edits.push_back(ed);
        const QString id = cmds[r].id;
        QObject::connect(ed, &QKeySequenceEdit::editingFinished, &dlg, [&S, ed, id, note, &edits, &cmds] {
            const QKeySequence ks = ed->keySequence().isEmpty() ? QKeySequence() : QKeySequence(ed->keySequence()[0]);
            const QString lost = S.set(id, ks);
            note->setText(lost.isEmpty() ? "저장했습니다." : QString("저장했습니다. '%1' 의 단축키는 해제되었습니다.").arg(lost));
            for (int i = 0; i < cmds.size(); ++i) edits[i]->setKeySequence(S.current(cmds[i].id));
        });
    }
    table->resizeRowsToContents();

    QObject::connect(search, &QLineEdit::textChanged, &dlg, [table](const QString& t) {
        for (int r = 0; r < table->rowCount(); ++r) {
            const bool hit = t.isEmpty() || table->item(r, 1)->text().contains(t, Qt::CaseInsensitive) ||
                             table->item(r, 0)->text().contains(t, Qt::CaseInsensitive);
            table->setRowHidden(r, !hit);
        }
    });

    auto* row = new QHBoxLayout;
    auto* rs = new QPushButton("선택 항목 기본값");
    auto* ra = new QPushButton("모두 기본값으로");
    auto* ok = new QPushButton("닫기");
    row->addWidget(rs);
    row->addWidget(ra);
    row->addStretch();
    row->addWidget(ok);
    lay->addLayout(row);
    auto refresh = [&] {
        for (int i = 0; i < cmds.size(); ++i) edits[i]->setKeySequence(S.current(cmds[i].id));
    };
    QObject::connect(rs, &QPushButton::clicked, &dlg, [&] {
        const int r = table->currentRow();
        if (r < 0) return;
        S.reset(cmds[r].id);
        refresh();
    });
    QObject::connect(ra, &QPushButton::clicked, &dlg, [&] {
        S.resetAll();
        refresh();
        note->setText("모든 단축키를 기본값으로 되돌렸습니다.");
    });
    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
}
