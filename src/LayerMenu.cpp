#include <functional>
#include <algorithm>
#include "LayerMenu.h"
#include "Blend.h"
#include "Document.h"

#include <QAction>
#include <QActionGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPixmap>

namespace LayerMenu {

static QIcon swatch(const QColor& c) {
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(c);
    p.setPen(QColor(0, 0, 0, 120));
    p.drawRoundedRect(1, 1, 12, 12, 3, 3);
    return QIcon(pm);
}

void populate(QMenu* m, Document* doc, const std::function<int()>& layerFn, const std::function<int()>& frameFn) {
    auto L = [doc, layerFn]() -> Layer& { return doc->layer(std::clamp(layerFn(), 0, doc->layerCount() - 1)); };
    auto changed = [doc] { doc->notifyStructure(); };

    // ---- 프리 / 포스트 비헤이비어 ----
    static const char* behaviors[] = {"없음", "반복", "핑퐁 (왕복)", "정지 (마지막 이미지 유지)"};
    for (int which = 0; which < 2; ++which) {
        QMenu* sub = m->addMenu(which == 0 ? "프리 비헤이비어 (첫 이미지 이전)" : "포스트 비헤이비어 (마지막 이미지 이후)");
        auto* grp = new QActionGroup(sub);
        QObject::connect(sub, &QMenu::aboutToShow, sub, [sub, grp, which, L] {
            const int cur = which == 0 ? L().preBehavior : L().postBehavior;
            for (int i = 0; i < grp->actions().size(); ++i) grp->actions()[i]->setChecked(i == cur);
        });
        for (int i = 0; i < 4; ++i) {
            QAction* a = sub->addAction(QString::fromUtf8(behaviors[i]));
            a->setCheckable(true);
            grp->addAction(a);
            QObject::connect(a, &QAction::triggered, sub, [=] {
                (which == 0 ? L().preBehavior : L().postBehavior) = i;
                changed();
            });
        }
    }
    // ---- 멀티플레인: 카메라를 얼마나 따라갈지 ----
    {
        QMenu* mp = m->addMenu("멀티플레인 (카메라 따라가기)");
        struct Preset { const char* name; int pct; };
        static const Preset presets[] = {
            {"먼 배경 (25%)", 25}, {"배경 (50%)", 50}, {"중간 (75%)", 75},
            {"기준면 (100%)", 100}, {"앞쪽 (150%)", 150}, {"맨 앞 (200%)", 200}, {"고정 (0%)", 0}};
        auto* grp = new QActionGroup(mp);
        QVector<QAction*> acts;
        for (const Preset& pr : presets) {
            QAction* a = mp->addAction(QString::fromUtf8(pr.name));
            a->setCheckable(true);
            grp->addAction(a);
            acts.push_back(a);
            const int pct = pr.pct;
            QObject::connect(a, &QAction::triggered, mp, [=] {
                L().camFollow = pct;
                changed();
            });
        }
        mp->addSeparator();
        QAction* custom = mp->addAction("직접 넣기...");
        QObject::connect(custom, &QAction::triggered, mp, [=] {
            bool ok = false;
            const int v = QInputDialog::getInt(nullptr, QStringLiteral("카메라 따라가기"),
                                               QStringLiteral("0 ~ 400 %  (100 = 카메라와 똑같이)"),
                                               L().camFollow, 0, 400, 5, &ok);
            if (!ok) return;
            L().camFollow = v;
            changed();
        });
        QObject::connect(mp, &QMenu::aboutToShow, mp, [acts, L] {
            static const int vals[] = {25, 50, 75, 100, 150, 200, 0};
            for (int i = 0; i < acts.size(); ++i) acts[i]->setChecked(L().camFollow == vals[i]);
        });
    }

    QAction* endHere = m->addAction("현재 프레임에서 레이어 끝내기");
    QObject::connect(endHere, &QAction::triggered, m, [=] {
        L().endFrame = frameFn() + 1;
        changed();
    });
    QAction* endClear = m->addAction("레이어 끝 해제 (프로젝트 끝까지)");
    QObject::connect(endClear, &QAction::triggered, m, [=] {
        L().endFrame = -1;
        changed();
    });
    m->addSeparator();

    // ---- 블렌딩 모드 ----
    QMenu* bm = m->addMenu("블렌딩 모드");
    auto* bg = new QActionGroup(bm);
    QObject::connect(bm, &QMenu::aboutToShow, bm, [bg, L] {
        for (int i = 0; i < bg->actions().size(); ++i) bg->actions()[i]->setChecked(i == L().blend);
    });
    for (int i = 0; i < Blend::Count; ++i) {
        QAction* a = bm->addAction(QString("%1  (%2)").arg(Blend::name(i), Blend::englishName(i)));
        a->setCheckable(true);
        a->setToolTip(Blend::description(i));
        a->setStatusTip(Blend::description(i));
        bg->addAction(a);
        QObject::connect(a, &QAction::triggered, bm, [=] {
            L().blend = i;
            changed();
        });
    }

    // ---- 컬러 그룹 ----
    QMenu* cg = m->addMenu("컬러 그룹");
    QAction* none = cg->addAction("없음");
    QObject::connect(none, &QAction::triggered, cg, [=] {
        L().colorGroup = -1;
        changed();
    });
    for (int i = 0; i < Document::colorGroupCount(); ++i) {
        QAction* a = cg->addAction(swatch(Document::colorGroupColor(i)), Document::colorGroupName(i));
        QObject::connect(a, &QAction::triggered, cg, [=] {
            L().colorGroup = i;
            changed();
        });
    }

    // ---- 스텐실 / 알파 보존 ----
    QAction* st = m->addAction("스텐실로 사용");
    st->setCheckable(true);
    QAction* si = m->addAction("스텐실 반전 (투명한 곳에만 그리기)");
    si->setCheckable(true);
    QAction* pa = m->addAction("알파 채널 보존 (칠해진 곳에만 칠하기)");
    pa->setCheckable(true);
    QAction* co = m->addAction("레이어 접기 / 펼치기");
    QObject::connect(m, &QMenu::aboutToShow, m, [=] {
        st->setChecked(L().stencil);
        si->setChecked(L().stencilInvert);
        pa->setChecked(L().preserveAlpha);
    });
    QObject::connect(st, &QAction::triggered, m, [=](bool on) { L().stencil = on; changed(); });
    QObject::connect(si, &QAction::triggered, m, [=](bool on) { L().stencilInvert = on; changed(); });
    QObject::connect(pa, &QAction::triggered, m, [=](bool on) { L().preserveAlpha = on; changed(); });
    QObject::connect(co, &QAction::triggered, m, [=] { L().collapsed = !L().collapsed; changed(); });
    m->addSeparator();

    // ---- 인스턴스 이름 / 이미지 마크 ----
    QAction* rn = m->addAction("인스턴스 이름 변경");
    QObject::connect(rn, &QAction::triggered, m, [=] {
        const int l = std::clamp(layerFn(), 0, doc->layerCount() - 1);
        const int k = doc->keyIndexAt(l, frameFn());
        if (k < 0) return;
        bool ok = false;
        const QString n = QInputDialog::getText(nullptr, "인스턴스 이름", "이름 (비우면 이름 없음):", QLineEdit::Normal,
                                                doc->layer(l).instanceNames.value(k), &ok);
        if (!ok) return;
        if (n.isEmpty()) doc->layer(l).instanceNames.remove(k);
        else doc->layer(l).instanceNames[k] = n;
        doc->notifyChanged();
    });
    // ---- 퍼펫 레이어 ----
    m->addSeparator();
    QMenu* pm = m->addMenu("퍼펫 레이어");
    QAction* pOn = pm->addAction("퍼펫 레이어로 사용");
    pOn->setCheckable(true);
    QAction* pKey = pm->addAction("현재 프레임에 퍼펫 키 설정...");
    QAction* pDel = pm->addAction("현재 프레임의 퍼펫 키 삭제");
    QAction* pPiv = pm->addAction("피벗(회전 중심) 설정...");
    QMenu* pPar = pm->addMenu("부모 레이어 연결");
    QObject::connect(pm, &QMenu::aboutToShow, pm, [=] {
        const int l = std::clamp(layerFn(), 0, doc->layerCount() - 1);
        const bool on = doc->layer(l).puppet;
        pOn->setChecked(on);
        pKey->setEnabled(on);
        pDel->setEnabled(on && doc->layer(l).pkeys.contains(frameFn()));
        pPiv->setEnabled(on);
        pPar->setEnabled(on);
        pPar->clear();
        QAction* none = pPar->addAction("연결 안 함");
        QObject::connect(none, &QAction::triggered, pPar, [=] { doc->setPuppetParent(l, -1); });
        for (int i = 0; i < doc->layerCount(); ++i) {
            if (i == l || !doc->layer(i).puppet) continue;
            QAction* a = pPar->addAction(doc->layer(i).name);
            a->setCheckable(true);
            a->setChecked(doc->puppetParentLayer(l) == i);
            QObject::connect(a, &QAction::triggered, pPar, [=] { doc->setPuppetParent(l, i); });
        }
    });
    QObject::connect(pOn, &QAction::triggered, pm, [=](bool on) { doc->setPuppet(std::clamp(layerFn(), 0, doc->layerCount() - 1), on); });
    QObject::connect(pDel, &QAction::triggered, pm, [=] { doc->removePuppetKey(layerFn(), frameFn()); });
    QObject::connect(pPiv, &QAction::triggered, pm, [=] {
        const int l = std::clamp(layerFn(), 0, doc->layerCount() - 1);
        QDialog dlg;
        dlg.setWindowTitle("피벗 설정");
        auto* f = new QFormLayout(&dlg);
        auto* x = new QDoubleSpinBox; x->setRange(0, 16384); x->setDecimals(1);
        auto* y = new QDoubleSpinBox; y->setRange(0, 16384); y->setDecimals(1);
        const Layer& Ly = doc->layer(l);
        x->setValue(Ly.pivotX < 0 ? doc->size().width() / 2.0 : Ly.pivotX);
        y->setValue(Ly.pivotY < 0 ? doc->size().height() / 2.0 : Ly.pivotY);
        f->addRow("X (그림 좌표)", x);
        f->addRow("Y (그림 좌표)", y);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        f->addRow(bb);
        QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() == QDialog::Accepted) doc->setPuppetPivot(l, x->value(), y->value());
    });
    QObject::connect(pKey, &QAction::triggered, pm, [=] {
        const int l = std::clamp(layerFn(), 0, doc->layerCount() - 1), fr = frameFn();
        const CameraKey cur = doc->puppetAt(l, fr);
        QDialog dlg;
        dlg.setWindowTitle(QString("퍼펫 키 - 프레임 %1").arg(fr + 1));
        auto* f = new QFormLayout(&dlg);
        auto mk = [&](double lo, double hi, double v, int dec) {
            auto* s = new QDoubleSpinBox; s->setRange(lo, hi); s->setDecimals(dec); s->setValue(v); return s;
        };
        auto* x = mk(-16384, 16384, cur.x, 1);
        auto* y = mk(-16384, 16384, cur.y, 1);
        auto* sc = mk(1, 2000, cur.scale * 100, 1);
        auto* rot = mk(-3600, 3600, cur.rotation, 1);
        auto* ease = new QComboBox;
        ease->addItems({"직선", "느리게 시작", "느리게 끝", "느리게 시작·끝", "계단"});
        ease->setCurrentIndex(std::clamp(cur.ease, 0, 4));
        f->addRow("X 이동", x);
        f->addRow("Y 이동", y);
        f->addRow("크기 (%)", sc);
        f->addRow("회전 (°)", rot);
        f->addRow("다음 키까지 속도", ease);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        f->addRow(bb);
        QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;
        CameraKey k;
        k.x = x->value(); k.y = y->value(); k.scale = sc->value() / 100.0; k.rotation = rot->value(); k.ease = ease->currentIndex();
        doc->setPuppetKey(l, fr, k);
    });
    m->addSeparator();
    QMenu* mk = m->addMenu("이미지 마크");
    QObject::connect(mk, &QMenu::aboutToShow, mk, [=] {
        mk->clear();
        const auto& cols = doc->markColors();
        for (int i = 0; i < cols.size(); ++i) {
            QAction* a = mk->addAction(swatch(cols[i].color), cols[i].name + " 마크 지정");
            QObject::connect(a, &QAction::triggered, mk, [=] { doc->setImageMark(layerFn(), frameFn(), i); });
        }
        mk->addSeparator();
        QAction* rm = mk->addAction("현재 이미지의 마크 제거");
        QObject::connect(rm, &QAction::triggered, mk, [=] { doc->setImageMark(layerFn(), frameFn(), -1); });
        QAction* rl = mk->addAction("현재 레이어의 마크 모두 제거");
        QObject::connect(rl, &QAction::triggered, mk, [=] {
            doc->layer(std::clamp(layerFn(), 0, doc->layerCount() - 1)).imageMarks.clear();
            doc->notifyChanged();
        });
        QAction* ra = mk->addAction("클립의 마크 모두 제거");
        QObject::connect(ra, &QAction::triggered, mk, [=] {
            for (int l = 0; l < doc->layerCount(); ++l) doc->layer(l).imageMarks.clear();
            doc->notifyChanged();
        });
    });
}

} // namespace LayerMenu
