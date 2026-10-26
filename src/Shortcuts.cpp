#include "Shortcuts.h"

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

Shortcuts::Shortcuts() {
    QSettings st("Ony", "Ony");
    st.beginGroup("shortcuts");
    for (const QString& k : st.childKeys()) {
        QString id = k;
        id.replace('|', '/');
        overrides_[id] = st.value(k).toString();
    }
}

Shortcuts& Shortcuts::instance() {
    static Shortcuts s;
    return s;
}

void Shortcuts::applyTo(Cmd& c) {
    if (!c.action) return;
    c.action->setShortcut(current(c.id));
}

QKeySequence Shortcuts::current(const QString& id) const {
    if (overrides_.contains(id)) return QKeySequence::fromString(overrides_[id]);
    for (const Cmd& c : cmds_)
        if (c.id == id) return c.def;
    return QKeySequence();
}

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

QAction* Shortcuts::add(QWidget* owner, const QString& category, const QString& label, const QKeySequence& def, std::function<void()> fn) {
    auto* a = new QAction(label, owner);
    a->setShortcutContext(Qt::WindowShortcut);
    owner->addAction(a);
    QObject::connect(a, &QAction::triggered, owner, [fn] { fn(); });
    registerCmd(a, category, label, def);
    return a;
}

void Shortcuts::adopt(QAction* a, const QString& category) {
    registerCmd(a, category, cleanLabel(a->text()), a->shortcut());
}

void Shortcuts::persist(const QString& id) {
    QSettings st("Ony", "Ony");
    st.beginGroup("shortcuts");
    QString k = id;
    k.replace('/', '|');
    if (overrides_.contains(id)) st.setValue(k, overrides_[id]);
    else st.remove(k);
}

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

void Shortcuts::reset(const QString& id) {
    for (const Cmd& c : cmds_)
        if (c.id == id) {
            set(id, c.def);
            return;
        }
}

void Shortcuts::resetAll() {
    const auto ids = overrides_.keys();
    overrides_.clear();
    for (const QString& id : ids) persist(id);
    for (Cmd& c : cmds_) applyTo(c);
    emit changed();
}

// ---------------------------------------------------------------- 설정 창
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
    const auto cmds = S.commands();
    table->setRowCount(cmds.size());
    for (int r = 0; r < cmds.size(); ++r) {
        table->setItem(r, 0, new QTableWidgetItem(cmds[r].category));
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
