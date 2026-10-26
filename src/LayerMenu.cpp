#include <algorithm>
#include "LayerMenu.h"
#include "Blend.h"
#include "Document.h"

#include <QAction>
#include <QActionGroup>
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
