#include "Privacy.h"

#include <QCheckBox>
#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace Privacy {

static QSettings& cfg() {
    static QSettings s("Ony", "Ony");
    return s;
}

bool errorReportsAllowed() { return cfg().value("privacy/errors", false).toBool(); }
bool usageStatsAllowed() { return cfg().value("privacy/usage", false).toBool(); }

static QFrame* card(QVBoxLayout* parent, const QString& title, const QString& body, QCheckBox*& box) {
    auto* f = new QFrame;
    f->setObjectName("privacyCard");
    f->setStyleSheet("#privacyCard{background:rgba(128,128,128,0.16);border-radius:10px;}");
    auto* l = new QVBoxLayout(f);
    l->setContentsMargins(16, 12, 16, 14);
    box = new QCheckBox(title);
    QFont bf = box->font();
    bf.setBold(true);
    bf.setPointSizeF(bf.pointSizeF() + 1);
    box->setFont(bf);
    l->addWidget(box);
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    l->addWidget(line);
    auto* t = new QLabel(body);
    t->setWordWrap(true);
    l->addWidget(t);
    parent->addWidget(f);
    return f;
}

void showDialog(QWidget* parent) {
    QDialog dlg(parent);
    dlg.setWindowTitle("개인정보 설정");
    dlg.setMinimumWidth(460);
    auto* lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(28, 24, 28, 22);
    lay->setSpacing(12);
    auto* title = new QLabel("개인정보 설정");
    QFont tf = title->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() + 5);
    title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);
    lay->addWidget(title);
    auto* intro = new QLabel("ONY 를 더 좋게 만드는 데 협조해 주시겠어요?\n아래 항목은 각각 선택할 수 있습니다.");
    intro->setAlignment(Qt::AlignCenter);
    intro->setWordWrap(true);
    lay->addWidget(intro);

    QCheckBox *errBox = nullptr, *useBox = nullptr;
    card(lay, "오류 개선에 협조",
         "오류가 발생하면 어떤 오류인지에 대한 정보를 익명으로 보냅니다.\n"
         "작품 내용(그림, 파일 이름 등)은 포함되지 않으며, 오류를 고치는 데에만 쓰입니다.", errBox);
    card(lay, "품질 향상에 협조",
         "어떤 기능을 얼마나 자주 쓰는지 같은 사용 현황을 익명으로 보냅니다.\n"
         "새 기능을 만들고 사용 경험을 개선하는 데 쓰입니다.", useBox);
    errBox->setChecked(cfg().value("privacy/errors", true).toBool());
    useBox->setChecked(cfg().value("privacy/usage", true).toBool());

    auto* note = new QLabel("[도움말] > [개인정보 설정]에서 언제든지 바꿀 수 있습니다.\n"
                            "참고: 이 버전에는 정보를 받는 서버가 아직 연결되어 있지 않아, 지금은 선택 내용만 저장되고 실제로 전송되는 정보는 없습니다.");
    note->setWordWrap(true);
    note->setAlignment(Qt::AlignCenter);
    QFont nf = note->font();
    nf.setPointSizeF(nf.pointSizeF() - 1);
    note->setFont(nf);
    lay->addWidget(note);

    auto* ok = new QPushButton("확인");
    ok->setDefault(true);
    ok->setMinimumHeight(36);
    lay->addWidget(ok);
    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();

    cfg().setValue("privacy/errors", errBox->isChecked());
    cfg().setValue("privacy/usage", useBox->isChecked());
    cfg().setValue("privacy/asked", true);
}

void askIfNeeded(QWidget* parent) {
    if (cfg().value("privacy/asked", false).toBool()) return;
    showDialog(parent);
}

} // namespace Privacy
