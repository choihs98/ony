#include <cmath>
#include <functional>
#include <algorithm>
#include <QCheckBox>
#include "Extras.h"
#include "Blend.h"
#include "Document.h"
#include "Warp.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtMath>
#include <queue>

namespace Extras {

QImage scanClean(const QImage& scan, int threshold, int despeckle, QColor ink) {
    const QImage g = scan.convertToFormat(QImage::Format_Grayscale8);
    const int w = g.width(), h = g.height();
    threshold = std::clamp(threshold, 1, 255);
    QVector<quint8> mask(w * h, 0);
    for (int y = 0; y < h; ++y) {
        const uchar* row = g.constScanLine(y);
        for (int x = 0; x < w; ++x) mask[y * w + x] = row[x] < threshold ? 1 : 0;
    }
    if (despeckle > 0) {
        QVector<quint8> seen(w * h, 0);
        for (int i = 0; i < w * h; ++i) {
            if (!mask[i] || seen[i]) continue;
            QVector<int> comp;
            std::queue<int> q;
            q.push(i);
            seen[i] = 1;
            while (!q.empty()) {
                const int cur = q.front();
                q.pop();
                comp.push_back(cur);
                const int cx = cur % w, cy = cur / w;
                const int nb[4][2] = {{cx - 1, cy}, {cx + 1, cy}, {cx, cy - 1}, {cx, cy + 1}};
                for (const auto& n : nb) {
                    if (n[0] < 0 || n[1] < 0 || n[0] >= w || n[1] >= h) continue;
                    const int ni = n[1] * w + n[0];
                    if (mask[ni] && !seen[ni]) {
                        seen[ni] = 1;
                        q.push(ni);
                    }
                }
            }
            if (comp.size() <= despeckle)
                for (int c : comp) mask[c] = 0;
        }
    }
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    for (int y = 0; y < h; ++y) {
        const uchar* row = g.constScanLine(y);
        QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            if (!mask[y * w + x]) continue;
            // 어두울수록 진하게
            const int a = std::clamp(255 - row[x] * 255 / threshold + 80, 80, 255);
            QColor c = ink;
            c.setAlpha(a);
            dst[x] = qPremultiply(c.rgba());
        }
    }
    return out;
}

bool exportXdts(const Document* doc, const QString& path, QString* error) {
    QJsonArray tracks, names;
    const int n = doc->layerCount();
    for (int ci = 0; ci < n; ++ci) {
        const int l = n - 1 - ci; // 위 레이어가 왼쪽 열
        QJsonArray frames;
        int keyNo = 0;
        bool any = false;
        int last = -2;
        for (int f = 0; f < doc->frameCount(); ++f) {
            if (!doc->layer(l).frames[f].isNull()) {
                ++keyNo;
                any = true;
                last = f;
                QJsonObject cell{{"id", 0}, {"values", QJsonArray{QString::number(keyNo)}}};
                frames.append(QJsonObject{{"data", QJsonArray{cell}}, {"frame", f}});
            }
        }
        if (any) {
            QJsonObject end{{"id", 0}, {"values", QJsonArray{QStringLiteral("SYMBOL_NULL_CELL")}}};
            if (last + 1 >= 0) {
                // 마지막 키 노출이 끝나는 지점에 빈 셀 기록 (전체 길이보다 작을 때)
                int endFrame = doc->frameCount();
                if (endFrame > last) frames.append(QJsonObject{{"data", QJsonArray{end}}, {"frame", endFrame}});
            }
        }
        tracks.append(QJsonObject{{"trackNo", ci}, {"frames", frames}});
        names.append(doc->layer(l).name);
    }
    QJsonObject field{{"fieldId", 0}, {"tracks", tracks}};
    QJsonObject table{{"duration", doc->frameCount()},
                      {"name", QStringLiteral("TS1")},
                      {"fields", QJsonArray{field}},
                      {"timeTableHeaders", QJsonArray{QJsonObject{{"fieldId", 0}, {"names", names}}}}};
    QJsonObject root{{"version", 5},
                     {"header", QJsonObject{{"cut", QStringLiteral("1")}, {"scene", QStringLiteral("1")}}},
                     {"timeTables", QJsonArray{table}}};
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write("exchangeDigitalTimeSheet Save Data\n");
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return true;
}

// After Effects 로 넘기기: 레이어별로 키 이미지만 PNG로 뽑아 레이어 전용 폴더에 저장하고,
// 레이어 이름·순서·불투명도·블렌드모드·타임라인 옵션과(선택 시) 카메라 키프레임을 JSON 매니페스트로 함께 적는다.
bool exportAfterEffects(const Document* doc, const QString& folder, const QString& baseName, bool useCamera, QString* error) {
    QDir dir(folder);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (error) *error = QStringLiteral("폴더를 만들 수 없습니다.");
        return false;
    }
    QJsonArray layersJson;
    const int n = doc->layerCount();
    for (int l = 0; l < n; ++l) {
        const Layer& lay = doc->layer(l);
        // 레이어마다 자기 폴더에 "레이어이름_프레임번호.png" 로 키 이미지만 저장 (홀드 구간은 AE 쪽에서 inOut 으로 처리)
        const QString layerDir = QString("layer%1_%2").arg(l + 1, 2, 10, QChar('0')).arg(lay.name);
        dir.mkpath(layerDir);
        QJsonArray keysJson;
        for (int f = 0; f < doc->frameCount(); ++f) {
            if (lay.frames[f].isNull()) continue; // 키가 아니면(홀드 중) 건너뜀
            const QString fileName = QString("%1_%2.png").arg(lay.name).arg(f + 1, 4, 10, QChar('0'));
            if (!lay.frames[f].save(dir.filePath(layerDir + "/" + fileName), "PNG")) continue;
            keysJson.append(QJsonObject{{"frame", f}, {"file", layerDir + "/" + fileName}});
        }
        layersJson.append(QJsonObject{
            {"index", l},
            {"name", lay.name},
            {"visible", lay.visible},
            {"opacity", lay.opacity},
            {"blendMode", Blend::englishName(lay.blend)},
            {"folder", lay.folder},
            {"preBehavior", lay.preBehavior},   // 0 없음, 1 반복, 2 핑퐁, 3 정지(홀드)
            {"postBehavior", lay.postBehavior},
            {"endFrame", lay.endFrame},
            {"camFollowPercent", lay.camFollow}, // 멀티플레인: 카메라를 얼마나 따라가는지(%)
            {"keys", keysJson},
        });
    }
    // 카메라 키프레임(사용할 경우에만): AE 쪽에서 널(Null) 레이어 트랜스폼 키로 그대로 옮길 수 있는 형태
    QJsonArray cameraJson;
    const bool camOn = useCamera && doc->cameraEnabled();
    if (camOn) {
        const auto& ck = doc->cameraKeys();
        for (auto it = ck.cbegin(); it != ck.cend(); ++it) {
            const CameraKey& k = it.value();
            cameraJson.append(QJsonObject{
                {"frame", it.key()}, {"x", k.x}, {"y", k.y}, {"scale", k.scale}, {"rotation", k.rotation}, {"ease", k.ease}
            });
        }
    }
    const QJsonObject root{
        {"formatVersion", 1},
        {"generator", QStringLiteral("ONY")},
        {"project", baseName},
        {"fps", doc->fps()},
        {"frameCount", doc->frameCount()},
        {"width", doc->renderSize().width()},
        {"height", doc->renderSize().height()},
        {"cameraEnabled", camOn},
        {"cameraKeys", cameraJson},
        {"layers", layersJson},
    };
    QFile f(dir.filePath(baseName + QStringLiteral("_ae.json")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

} // namespace Extras

// ---------------------------------------------------------------- 스크립트
QString ScriptEngine::help() {
    return QStringLiteral(
        "■ 기본 (한 줄에 하나, # 은 주석)\n"
        "  echo 문장          화면에 글 남기기 ($변수 를 쓸 수 있음)\n"
        "  set 이름 = 식       숫자 변수 (예: set n = 10 + 2*3)\n"
        "  sets 이름 문장      글자 변수\n"
        "  get 이름 항목       문서에서 값 읽기 (frame frames fps layer layers width height keys)\n"
        "  vars               변수 목록 보기 / eval 식  계산해 보기\n"
        "■ 흐름\n"
        "  if 조건 ... else ... end\n"
        "  while 조건 ... end\n"
        "  for i = 1 to 10 [step 2] ... end      (안에서 $i 로 씀)\n"
        "  repeat A B 명령                        (한 줄 반복, {i} 가 값으로 바뀜)\n"
        "  do ... until 조건                      (한 번은 꼭 돌고, 조건이 참이 되면 멈춘다)\n"
        "  식에 쓸 수 있는 것: + - * / % ( ) < > <= >= == != and or not\n"
        "    abs min max int floor ceil round sign sqrt pow(a,b) exp log\n"
        "    sin cos tan asin acos atan(a[,b]) rnd() rnd(n) rnd(a,b) pi\n"
        "■ 긴 이름 (ony_…)\n"
        "  짧은 명령 대신 풀어 쓴 이름도 알아듣습니다 (대소문자 가리지 않음, \"따옴표\" 는 벗겨 줍니다)\n"
        "    ony_LayerCreate ony_LayerSelect ony_LayerDelete ony_LayerRename ony_LayerVisible\n"
        "    ony_LayerOpacity ony_LayerBlend ony_LayerLock ony_LayerMove ony_LayerDuplicate\n"
        "    ony_LayerMerge ony_LayerClear ony_GotoFrame ony_Exposure ony_FrameRate\n"
        "    ony_Rect ony_Line ony_Ellipse ony_SaveProject ony_SaveImage ony_Print ony_Version\n"
        "  바로 앞 명령의 결과는 $result 에 담깁니다\n"
        "  콘솔의 [열기]·[저장] 으로 .onys / .txt 스크립트 파일을 주고받을 수 있습니다\n"
        "■ 프로젝트 · 레이어\n"
        "  frame N | frames N | fps N\n"
        "  layer.new 이름 | layer.select N | layer.delete | layer.rename 이름\n"
        "  layer.visible 0/1 | layer.opacity 0-100 | layer.lock 0/1\n"
        "  layer.move ±1 | layer.duplicate | layer.mergedown | layer.blend N\n"
        "■ 프레임 · 키\n"
        "  frame.insert | frame.delete | frame.next | frame.prev\n"
        "  frame.copy A B 전체레이어0/1 | frame.paste [AT] [끼워넣기0/1]\n"
        "  key.new | key.clear | key.exposure N | key.reverse | key.repeat A B 횟수 왕복0/1\n"
        "■ 그리기\n"
        "  clear | rect X Y W H #rrggbb | ellipse X Y W H #rrggbb | line X1 Y1 X2 Y2 #rrggbb 굵기\n"
        "■ 효과\n"
        "  fx.invert fx.gray fx.bright N fx.blur N fx.gauss N fx.contrast N fx.saturation N\n"
        "  fx.hue N fx.sepia fx.posterize N fx.threshold N fx.wave N fx.swirl N fx.bulge N\n"
        "  fx.keywhite N fx.keyblack N fx.keygreen N fx.motionblur N fx.echo N fx.noise N\n"
        "  fx.vignette N fx.shadow N fx.pixelate N fx.emboss fx.edge fx.outline N fx.sharpen N\n"
        "  fx.curve N fx.levels N | flip.h | flip.v\n"
        "■ 카메라 · 그 밖\n"
        "  camera.on | camera.off | camera.key 프레임 X Y 배율% 회전 이징\n"
        "  bookmark [프레임] | lib.add 이름 [분류]\n"
        "  save 경로 | savecopy 경로 | export.png 경로\n"
        "■ 창에서 처리하는 명령 (스크립트 콘솔에서 쓸 때)\n"
        "  select.all | select.none | select.invert | select.rect X Y W H\n"
        "  select.grow N | select.feather N | color A #rrggbb | tool 이름 | zoom N | msg 문장");
}

// 현재 선택된 키 이미지를 함수로 고치고 실행 취소에 기록한다 (스크립트의 그리기 명령용)
bool ScriptEngine::drawOnCurrent(const std::function<void(QImage&)>& fn) {
    const int l = doc_->currentLayer(), f = doc_->currentFrame();
    if (doc_->layer(l).locked) return false;
    const int k = doc_->keyIndexAt(l, f);
    QImage img = k >= 0 ? doc_->keyImage(l, k) : QImage(doc_->size(), QImage::Format_ARGB32_Premultiplied);
    if (k < 0) img.fill(Qt::transparent);
    fn(img);
    doc_->pushKeyEdit(l, f, doc_->layer(l).frames[f], img, QStringLiteral("스크립트"));
    return true;
}

// 스크립트 전체를 한 줄씩 실행한다
QString ScriptEngine::run(const QString& script) {
    QStringList lines = script.split('\n');
    steps_ = 0;
    const QString log = runBlock(lines, 0, lines.size(), 0);
    return log.isEmpty() ? QStringLiteral("완료\n") : log;
}

// if / while / for 의 짝이 맞는 end 줄 번호 (없으면 -1)
int ScriptEngine::matchEnd(const QStringList& lines, int start, int to) const {
    int depth = 0;
    const QString head = lines[start].trimmed().section(QRegularExpression("\\s+"), 0, 0).toLower();
    // do … until 블록은 end 가 아니라 until 로 닫는다
    if (head == "do") {
        for (int i = start; i < to; ++i) {
            const QString t = lines[i].trimmed().section(QRegularExpression("\\s+"), 0, 0).toLower();
            if (t == "do") ++depth;
            else if (t == "until") {
                --depth;
                if (depth == 0) return i;
            }
        }
        return -1;
    }
    for (int i = start; i < to; ++i) {
        const QString t = lines[i].trimmed().section(QRegularExpression("\\s+"), 0, 0).toLower();
        if (t == "if" || t == "while" || t == "for") ++depth;
        else if (t == "do") {   // do 블록은 통째로 건너뛴다
            const int u = matchEnd(lines, i, to);
            if (u < 0) return -1;
            i = u;
        } else if (t == "end") {
            --depth;
            if (depth == 0) return i;
        }
    }
    return -1;
}

// 지정된 줄 구간(블록)을 실행한다 (if/while/for 재귀 처리, 깊이 제한 있음)
QString ScriptEngine::runBlock(const QStringList& lines, int from, int to, int depth) {
    if (depth > 16) return QStringLiteral("블록이 너무 깊습니다\n");
    QString log;
    for (int i = from; i < to; ++i) {
        if (++steps_ > 500000) return log + QStringLiteral("명령이 너무 많습니다 (무한 반복?)\n");
        const QString line = lines[i].trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const QStringList t = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        const QString head = t[0].toLower();

        if (head == "if") {
            const int endIdx = matchEnd(lines, i, to);
            if (endIdx < 0) return log + QString("%1: if 에 맞는 end 가 없습니다\n").arg(i + 1);
            // else 찾기 (같은 깊이)
            int elseIdx = -1, d = 0;
            for (int k = i; k < endIdx; ++k) {
                const QString kt = lines[k].trimmed().section(QRegularExpression("\\s+"), 0, 0).toLower();
                if (kt == "if" || kt == "while" || kt == "for") ++d;
                else if (kt == "end") --d;
                else if (kt == "else" && d == 1) { elseIdx = k; break; }
            }
            const QString cond = line.section(QRegularExpression("\\s+"), 1);
            const bool ok = eval(cond) != 0.0;
            if (ok) log += runBlock(lines, i + 1, elseIdx >= 0 ? elseIdx : endIdx, depth + 1);
            else if (elseIdx >= 0) log += runBlock(lines, elseIdx + 1, endIdx, depth + 1);
            i = endIdx;
            continue;
        }
        if (head == "while") {
            const int endIdx = matchEnd(lines, i, to);
            if (endIdx < 0) return log + QString("%1: while 에 맞는 end 가 없습니다\n").arg(i + 1);
            const QString cond = line.section(QRegularExpression("\\s+"), 1);
            int guard = 0;
            while (eval(cond) != 0.0) {
                if (++guard > 100000 || ++steps_ > 500000) {
                    log += QStringLiteral("while 이 너무 오래 돕니다\n");
                    break;
                }
                log += runBlock(lines, i + 1, endIdx, depth + 1);
            }
            i = endIdx;
            continue;
        }
        if (head == "for") {
            // for i = 1 to 10  [step 2]
            const int endIdx = matchEnd(lines, i, to);
            if (endIdx < 0) return log + QString("%1: for 에 맞는 end 가 없습니다\n").arg(i + 1);
            if (t.size() < 5 || t[2] != "=") {
                log += QString("%1: 사용법: for 변수 = 시작 to 끝 [step N]\n").arg(i + 1);
                i = endIdx;
                continue;
            }
            const QString v = t[1];
            const QString rest = line.section(QRegularExpression("\\s+"), 3);
            const int toPos = rest.toLower().indexOf(" to ");
            if (toPos < 0) {
                log += QString("%1: for 에 to 가 없습니다\n").arg(i + 1);
                i = endIdx;
                continue;
            }
            const double a = eval(rest.left(toPos));
            QString tail = rest.mid(toPos + 4);
            double stepv = 1;
            const int sp = tail.toLower().indexOf(" step ");
            if (sp >= 0) {
                stepv = eval(tail.mid(sp + 6));
                tail = tail.left(sp);
            }
            const double b = eval(tail);
            if (stepv == 0) stepv = 1;
            int guard = 0;
            for (double x = a; (stepv > 0 ? x <= b : x >= b); x += stepv) {
                if (++guard > 100000 || ++steps_ > 500000) {
                    log += QStringLiteral("for 가 너무 오래 돕니다\n");
                    break;
                }
                vars_[v] = x;
                log += runBlock(lines, i + 1, endIdx, depth + 1);
            }
            i = endIdx;
            continue;
        }
        if (head == "do") {
            // do … until 조건. 본문을 한 번 돌고 조건이 참이 될 때까지 되풀이한다.
            const int untilIdx = matchEnd(lines, i, to);
            if (untilIdx < 0) return log + QString("%1: do 에 맞는 until 이 없습니다\n").arg(i + 1);
            const QString cond = lines[untilIdx].trimmed().section(QRegularExpression("\\s+"), 1);
            int guard = 0;
            do {
                if (++guard > 100000 || ++steps_ > 500000) {
                    log += QStringLiteral("do 가 너무 오래 돕니다\n");
                    break;
                }
                log += runBlock(lines, i + 1, untilIdx, depth + 1);
            } while (cond.isEmpty() ? false : eval(cond) == 0.0);
            i = untilIdx;
            continue;
        }
        if (head == "else" || head == "end" || head == "until") continue;

        const QString r = runLine(line, depth);
        // 바로 앞 명령의 결과를 $result 로 쓸 수 있게 둔다
        svars_[QStringLiteral("result")] = r;
        bool okNum = false;
        const double rd = r.toDouble(&okNum);
        vars_[QStringLiteral("result")] = okNum ? rd : 0.0;
        if (!r.isEmpty()) log += QString("%1: %2\n").arg(i + 1).arg(r);
    }
    return log;
}

// $이름 을 값으로 바꾼다
QString ScriptEngine::expand(const QString& s) const {
    QString out = s;
    static const QRegularExpression re("\\$([A-Za-z_][A-Za-z0-9_]*)");
    QRegularExpressionMatchIterator it = re.globalMatch(s);
    QVector<QPair<int, QPair<int, QString>>> repl;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString name = m.captured(1);
        QString v;
        if (svars_.contains(name)) v = svars_.value(name);
        else if (vars_.contains(name)) {
            const double d = vars_.value(name);
            v = (d == std::floor(d) && std::abs(d) < 1e15) ? QString::number(qint64(d)) : QString::number(d, 'g', 10);
        } else {
            v = QStringLiteral("0");
        }
        repl.push_back({int(m.capturedStart(0)), {int(m.capturedLength(0)), v}});
    }
    for (int k = repl.size() - 1; k >= 0; --k) out.replace(repl[k].first, repl[k].second.first, repl[k].second.second);
    return out;
}

// 아주 작은 식 계산기: or / and / 비교 / + - / * / % / 괄호 / 숫자 / $변수
namespace {
struct Expr {
    QString s;
    int i = 0;
    const QMap<QString, double>* vars = nullptr;
    void ws() { while (i < s.size() && s[i].isSpace()) ++i; }
    bool word(const char* w) {
        ws();
        const QString t = QString::fromLatin1(w);
        if (s.mid(i, t.size()).compare(t, Qt::CaseInsensitive) == 0) {
            const int after = i + t.size();
            if (after >= s.size() || !s[after].isLetterOrNumber()) {
                i = after;
                return true;
            }
        }
        return false;
    }
    bool sym(const char* w) {
        ws();
        const QString t = QString::fromLatin1(w);
        if (s.mid(i, t.size()) == t) {
            i += t.size();
            return true;
        }
        return false;
    }
    double orExpr() {
        double v = andExpr();
        while (word("or") || sym("||")) v = (v != 0 || andExpr() != 0) ? 1 : 0;
        return v;
    }
    double andExpr() {
        double v = cmp();
        while (word("and") || sym("&&")) v = (v != 0 && cmp() != 0) ? 1 : 0;
        return v;
    }
    double cmp() {
        double v = add();
        for (;;) {
            if (sym("<=")) v = v <= add() ? 1 : 0;
            else if (sym(">=")) v = v >= add() ? 1 : 0;
            else if (sym("==")) v = std::abs(v - add()) < 1e-9 ? 1 : 0;
            else if (sym("!=")) v = std::abs(v - add()) >= 1e-9 ? 1 : 0;
            else if (sym("<")) v = v < add() ? 1 : 0;
            else if (sym(">")) v = v > add() ? 1 : 0;
            else break;
        }
        return v;
    }
    double add() {
        double v = mul();
        for (;;) {
            if (sym("+")) v += mul();
            else if (sym("-")) v -= mul();
            else break;
        }
        return v;
    }
    double mul() {
        double v = unary();
        for (;;) {
            if (sym("*")) v *= unary();
            else if (sym("/")) {
                const double d = unary();
                v = std::abs(d) < 1e-12 ? 0 : v / d;
            } else if (sym("%")) {
                const double d = unary();
                v = std::abs(d) < 1e-12 ? 0 : std::fmod(v, d);
            } else break;
        }
        return v;
    }
    double unary() {
        ws();
        if (sym("-")) return -unary();
        if (sym("+")) return unary();
        if (word("not") || sym("!")) return unary() == 0 ? 1 : 0;
        return atom();
    }
    double atom() {
        ws();
        if (sym("(")) {
            const double v = orExpr();
            sym(")");
            return v;
        }
        if (word("abs")) { sym("("); const double v = orExpr(); sym(")"); return std::abs(v); }
        if (word("min")) { sym("("); const double a = orExpr(); sym(","); const double b = orExpr(); sym(")"); return std::min(a, b); }
        if (word("max")) { sym("("); const double a = orExpr(); sym(","); const double b = orExpr(); sym(")"); return std::max(a, b); }
        if (word("int")) { sym("("); const double v = orExpr(); sym(")"); return std::floor(v); }
        if (word("sin")) { sym("("); const double v = orExpr(); sym(")"); return std::sin(v); }
        if (word("cos")) { sym("("); const double v = orExpr(); sym(")"); return std::cos(v); }
        // 그 밖의 수학 함수들
        if (word("tan")) { sym("("); const double v = orExpr(); sym(")"); return std::tan(v); }
        if (word("asin")) { sym("("); const double v = orExpr(); sym(")"); return std::asin(std::clamp(v, -1.0, 1.0)); }
        if (word("acos")) { sym("("); const double v = orExpr(); sym(")"); return std::acos(std::clamp(v, -1.0, 1.0)); }
        if (word("atan")) {
            sym("(");
            const double a = orExpr();
            if (sym(",")) { const double b = orExpr(); sym(")"); return std::atan2(a, b); }
            sym(")");
            return std::atan(a);
        }
        if (word("sqrt")) { sym("("); const double v = orExpr(); sym(")"); return v <= 0 ? 0 : std::sqrt(v); }
        if (word("exp")) { sym("("); const double v = orExpr(); sym(")"); return std::exp(std::clamp(v, -700.0, 700.0)); }
        if (word("log")) { sym("("); const double v = orExpr(); sym(")"); return v <= 0 ? 0 : std::log(v); }
        if (word("pow")) { sym("("); const double a = orExpr(); sym(","); const double b = orExpr(); sym(")"); return std::pow(a, b); }
        if (word("floor")) { sym("("); const double v = orExpr(); sym(")"); return std::floor(v); }
        if (word("ceil")) { sym("("); const double v = orExpr(); sym(")"); return std::ceil(v); }
        if (word("round")) { sym("("); const double v = orExpr(); sym(")"); return std::floor(v + 0.5); }
        if (word("sign")) { sym("("); const double v = orExpr(); sym(")"); return v > 0 ? 1 : (v < 0 ? -1 : 0); }
        if (word("rnd")) {
            // Rnd(n) → 0..n 의 정수, Rnd(a,b) → a..b, Rnd() → 0..1 의 실수
            sym("(");
            if (sym(")")) return double(QRandomGenerator::global()->generateDouble());
            const double a = orExpr();
            if (sym(",")) {
                const double b = orExpr();
                sym(")");
                const int lo = int(std::floor(std::min(a, b))), hi = int(std::floor(std::max(a, b)));
                return lo + (hi > lo ? QRandomGenerator::global()->bounded(hi - lo + 1) : 0);
            }
            sym(")");
            const int n = int(std::floor(std::abs(a)));
            return n > 0 ? QRandomGenerator::global()->bounded(n + 1) : 0;
        }
        if (word("pi")) return 3.14159265358979323846;
        const int start = i;
        while (i < s.size() && (s[i].isDigit() || s[i] == '.')) ++i;
        if (i > start) return s.mid(start, i - start).toDouble();
        // 이름 (변수)
        const int ns = i;
        while (i < s.size() && (s[i].isLetterOrNumber() || s[i] == '_')) ++i;
        if (i > ns && vars) return vars->value(s.mid(ns, i - ns), 0.0);
        if (i == ns) ++i;   // 모르는 글자는 건너뛴다
        return 0;
    }
};
}

// 수식 문자열을 계산해 숫자 값으로 돌려준다
double ScriptEngine::eval(const QString& expr) const {
    Expr e;
    e.s = expand(expr);
    e.vars = &vars_;
    return e.orExpr();
}

// 긴 이름(ony_…) 을 짧은 ONY 명령으로 옮기는 표. 모르는 ony_ 이름은 호스트로 넘어간다.
static const QHash<QString, QString>& longNameAliases() {
    static const QHash<QString, QString> m = {
        {"ony_layercreate", "layer.new"},
        {"ony_layerselect", "layer.select"},
        {"ony_layercurrent", "layer.select"},
        {"ony_layerdelete", "layer.delete"},
        {"ony_layerrename", "layer.rename"},
        {"ony_layervisible", "layer.visible"},
        {"ony_layeropacity", "layer.opacity"},
        {"ony_layerblend", "layer.blend"},
        {"ony_layerlock", "layer.lock"},
        {"ony_layermove", "layer.move"},
        {"ony_layerduplicate", "layer.duplicate"},
        {"ony_layermerge", "layer.mergedown"},
        {"ony_layerclear", "clear"},
        {"ony_gotoframe", "frame"},
        {"ony_currentframe", "frame"},
        {"ony_imageclear", "key.clear"},
        {"ony_exposure", "key.exposure"},
        {"ony_framerate", "fps"},
        {"ony_rect", "rect"},
        {"ony_line", "line"},
        {"ony_ellipse", "ellipse"},
        {"ony_saveproject", "save"},
        {"ony_saveimage", "export.png"},
        {"ony_print", "print"},
        {"ony_version", "version"},
    };
    return m;
}

// 한 줄을 해석해서 명령을 실행한다 (ony_ 긴 이름은 먼저 짧은 명령으로 바꾼다)
QString ScriptEngine::runLine(const QString& rawLine, int depth) {
    if (depth > 16) return QStringLiteral("너무 깊습니다");
    QString line = expand(rawLine);
    {   // 긴 이름(ony_…) 을 짧은 명령으로 바꾸고, "따옴표" 를 벗긴다
        const int sp = line.indexOf(QRegularExpression("\\s"));
        const QString head = (sp < 0 ? line : line.left(sp)).trimmed();
        if (head.startsWith(QLatin1String("ony_"), Qt::CaseInsensitive)) {
            const QString mapped = longNameAliases().value(head.toLower());
            if (!mapped.isEmpty()) {
                QString tail = sp < 0 ? QString() : line.mid(sp);
                tail.replace('"', ' ');
                line = mapped + tail;
            }
        }
    }
    const QStringList t = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (t.isEmpty()) return {};
    const QString cmd = t[0].toLower();
    // 인자에 식을 써도 된다 (띄어쓰기 없이: $c*40+8)
    auto num = [&](int i, double def = 0) { return i < t.size() ? eval(t[i]) : def; };
    auto col = [&](int i) { return i < t.size() ? QColor(t[i]) : QColor(Qt::black); };
    const QString rest = line.section(QRegularExpression("\\s+"), 1);

    if (cmd == "echo" || cmd == "print") return rest;
    if (cmd == "set") {
        // set 이름 = 식   /   set 이름 식
        if (t.size() < 3) return QStringLiteral("사용법: set 이름 = 식");
        const QString name = t[1];
        const int eq = line.indexOf('=');
        const QString ex = eq >= 0 ? line.mid(eq + 1) : line.section(QRegularExpression("\\s+"), 2);
        vars_[name] = eval(ex);
        return {};
    }
    if (cmd == "sets") {   // 문자열 변수
        if (t.size() < 3) return QStringLiteral("사용법: sets 이름 문자열");
        svars_[t[1]] = line.section(QRegularExpression("\\s+"), 2);
        return {};
    }
    if (cmd == "vars") {
        QStringList out;
        for (auto it = vars_.cbegin(); it != vars_.cend(); ++it) out << QString("%1=%2").arg(it.key()).arg(it.value());
        for (auto it = svars_.cbegin(); it != svars_.cend(); ++it) out << QString("%1=\"%2\"").arg(it.key(), it.value());
        return out.isEmpty() ? QStringLiteral("(변수 없음)") : out.join(", ");
    }
    if (cmd == "eval") return QString::number(eval(rest));
    // ---- 문서에서 값을 읽어 변수에 담는다 ----
    if (cmd == "get") {
        if (t.size() < 3) return QStringLiteral("사용법: get 변수 항목  (항목: frame frames fps layer layers width height keys)");
        const QString what = t[2].toLower();
        double v = 0;
        if (what == "frame") v = doc_->currentFrame() + 1;
        else if (what == "frames") v = doc_->frameCount();
        else if (what == "fps") v = doc_->fps();
        else if (what == "layer") v = doc_->currentLayer();
        else if (what == "layers") v = doc_->layerCount();
        else if (what == "width") v = doc_->size().width();
        else if (what == "height") v = doc_->size().height();
        else if (what == "keys") {
            int n = 0;
            const Layer& L = doc_->layer(doc_->currentLayer());
            for (int f = 0; f < doc_->frameCount(); ++f) n += L.frames[f].isNull() ? 0 : 1;
            v = n;
        } else return QString("모르는 항목: %1").arg(what);
        vars_[t[1]] = v;
        return {};
    }
    if (cmd == "repeat") {
        if (t.size() < 4) return QStringLiteral("사용법: repeat A B 명령");
        const int a = int(num(1)), b = int(num(2));
        if (b - a > 10000) return QStringLiteral("반복 횟수가 너무 많습니다");
        const QString body = line.section(QRegularExpression("\\s+"), 3);
        QString log;
        for (int i = a; i <= b; ++i) {
            QString cur = body;
            cur.replace("{i}", QString::number(i));
            const QString r = runLine(cur, depth + 1);
            if (!r.isEmpty()) log += r + "; ";
        }
        return log;
    }
    if (cmd == "frame") { doc_->setCurrentFrame(int(num(1)) - 1); return {}; }
    if (cmd == "frames") { doc_->setFrameCount(int(num(1))); return {}; }
    if (cmd == "fps") { doc_->setFps(int(num(1, 24))); return {}; }
    if (cmd == "layer.new") { doc_->addLayer(rest.isEmpty() ? QStringLiteral("레이어") : rest); return {}; }
    if (cmd == "layer.select") {
        const int i = int(num(1)) - 1;
        if (i < 0 || i >= doc_->layerCount()) return QStringLiteral("없는 레이어");
        doc_->setCurrentLayer(i);
        return {};
    }
    if (cmd == "layer.delete") { doc_->removeLayer(doc_->currentLayer()); return {}; }
    if (cmd == "layer.rename") { doc_->layer(doc_->currentLayer()).name = rest; doc_->notifyStructure(); return {}; }
    if (cmd == "layer.visible") { doc_->layer(doc_->currentLayer()).visible = num(1, 1) != 0; doc_->notifyStructure(); return {}; }
    if (cmd == "layer.opacity") { doc_->layer(doc_->currentLayer()).opacity = std::clamp(num(1, 100), 0.0, 100.0) / 100.0; doc_->notifyChanged(); return {}; }
    if (cmd == "key.new" || cmd == "clear") {
        const int l = doc_->currentLayer(), f = doc_->currentFrame();
        if (doc_->layer(l).locked) return QStringLiteral("잠긴 레이어");
        QImage blank(doc_->size(), QImage::Format_ARGB32_Premultiplied);
        blank.fill(Qt::transparent);
        doc_->pushKeyEdit(l, f, doc_->layer(l).frames[f], blank, QStringLiteral("스크립트"));
        return {};
    }
    if (cmd == "key.clear") {
        const int l = doc_->currentLayer(), f = doc_->currentFrame();
        if (doc_->layer(l).locked || doc_->layer(l).frames[f].isNull()) return {};
        doc_->pushKeyEdit(l, f, doc_->layer(l).frames[f], QImage(), QStringLiteral("스크립트"));
        return {};
    }
    if (cmd == "rect" || cmd == "ellipse") {
        if (t.size() < 5) return QStringLiteral("사용법: %1 X Y W H 색").arg(cmd);
        const QRectF r(num(1), num(2), num(3), num(4));
        const QColor c = col(5);
        if (!drawOnCurrent([&](QImage& im) {
                QPainter p(&im);
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setPen(Qt::NoPen);
                p.setBrush(c);
                if (cmd == "rect") p.drawRect(r);
                else p.drawEllipse(r);
            }))
            return QStringLiteral("잠긴 레이어");
        return {};
    }
    if (cmd == "line") {
        if (t.size() < 5) return QStringLiteral("사용법: line X1 Y1 X2 Y2 색 굵기");
        const QColor c = col(5);
        const double w = num(6, 3);
        if (!drawOnCurrent([&](QImage& im) {
                QPainter p(&im);
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setPen(QPen(c, w, Qt::SolidLine, Qt::RoundCap));
                p.drawLine(QPointF(num(1), num(2)), QPointF(num(3), num(4)));
            }))
            return QStringLiteral("잠긴 레이어");
        return {};
    }
    if (cmd == "fx.invert") { doc_->transformCurrent(Effects::invert, "스크립트"); return {}; }
    if (cmd == "fx.gray") { doc_->transformCurrent(Effects::grayscale, "스크립트"); return {}; }
    if (cmd == "fx.bright") { const int v = int(num(1)); doc_->transformCurrent([v](const QImage& i) { return Effects::brightness(i, v); }, "스크립트"); return {}; }
    if (cmd == "fx.blur") { const int v = int(num(1, 3)); doc_->transformCurrent([v](const QImage& i) { return Effects::boxBlur(i, v); }, "스크립트"); return {}; }
    if (cmd == "flip.h") { doc_->transformCurrent(Effects::flipH, "스크립트"); return {}; }
    if (cmd == "flip.v") { doc_->transformCurrent(Effects::flipV, "스크립트"); return {}; }
    // ---- 효과 (데모5에서 넓힘) ----
    {
        static const QMap<QString, Effects::Type> kFx{
            {"fx.gauss", Effects::Type::GaussBlur}, {"fx.contrast", Effects::Type::Contrast},
            {"fx.saturation", Effects::Type::Saturation}, {"fx.hue", Effects::Type::HueShift},
            {"fx.sepia", Effects::Type::Sepia}, {"fx.posterize", Effects::Type::Posterize},
            {"fx.threshold", Effects::Type::Threshold}, {"fx.wave", Effects::Type::Wave},
            {"fx.swirl", Effects::Type::Swirl}, {"fx.bulge", Effects::Type::Bulge},
            {"fx.keywhite", Effects::Type::KeyWhite}, {"fx.keyblack", Effects::Type::KeyBlack},
            {"fx.keygreen", Effects::Type::KeyGreen}, {"fx.motionblur", Effects::Type::MotionBlurH},
            {"fx.motionblurv", Effects::Type::MotionBlurV}, {"fx.echo", Effects::Type::Echo},
            {"fx.noise", Effects::Type::Noise}, {"fx.vignette", Effects::Type::Vignette},
            {"fx.shadow", Effects::Type::DropShadow}, {"fx.pixelate", Effects::Type::Pixelate},
            {"fx.emboss", Effects::Type::Emboss}, {"fx.edge", Effects::Type::Edge},
            {"fx.outline", Effects::Type::Outline}, {"fx.sharpen", Effects::Type::Sharpen},
            {"fx.curve", Effects::Type::Curves}, {"fx.levels", Effects::Type::Levels},
        };
        const auto it = kFx.constFind(cmd);
        if (it != kFx.constEnd()) {
            Effects::Item item;
            item.type = it.value();
            const auto pi = Effects::paramInfo(item.type);
            item.param = t.size() > 1 ? int(num(1)) : pi.def;
            doc_->transformCurrent([item](const QImage& i) { return Effects::apply(i, item); }, QStringLiteral("스크립트"));
            return {};
        }
    }
    // ---- 레이어 ----
    if (cmd == "layer.move") { doc_->moveLayer(doc_->currentLayer(), int(num(1, 1))); return {}; }
    if (cmd == "layer.duplicate") { doc_->duplicateLayer(doc_->currentLayer()); return {}; }
    if (cmd == "layer.mergedown") { return doc_->mergeDown(doc_->currentLayer()) ? QString() : QStringLiteral("합칠 아래 레이어가 없습니다"); }
    if (cmd == "layer.blend") { doc_->layer(doc_->currentLayer()).blend = std::clamp(int(num(1)), 0, Blend::Count - 1); doc_->notifyStructure(); return {}; }
    if (cmd == "layer.lock") { doc_->layer(doc_->currentLayer()).locked = num(1) != 0; doc_->notifyStructure(); return {}; }
    // ---- 프레임 ----
    if (cmd == "frame.insert") { doc_->insertFrame(doc_->currentFrame()); return {}; }
    if (cmd == "frame.delete") { doc_->deleteFrame(doc_->currentFrame()); return {}; }
    if (cmd == "frame.next") { doc_->setCurrentFrame(doc_->currentFrame() + 1); return {}; }
    if (cmd == "frame.prev") { doc_->setCurrentFrame(doc_->currentFrame() - 1); return {}; }
    if (cmd == "frame.copy") { frameClip_ = doc_->copyFrames(doc_->currentLayer(), int(num(1)) - 1, int(num(2)) - 1, num(3) != 0); return {}; }
    if (cmd == "frame.paste") {
        if (frameClip_.isEmpty()) return QStringLiteral("복사한 프레임이 없습니다");
        return doc_->pasteFrames(doc_->currentLayer(), t.size() > 1 ? int(num(1)) - 1 : doc_->currentFrame(), frameClip_, num(2, 1) != 0)
                   ? QString() : QStringLiteral("붙여넣지 못했습니다");
    }
    if (cmd == "key.exposure") { doc_->setUniformExposure(doc_->currentLayer(), std::max(1, int(num(1, 1)))); return {}; }
    if (cmd == "key.reverse") { doc_->reverseKeys(doc_->currentLayer(), 0, doc_->frameCount() - 1); return {}; }
    if (cmd == "key.repeat") { doc_->repeatKeys(doc_->currentLayer(), int(num(1)) - 1, int(num(2)) - 1, std::max(1, int(num(3, 1))), num(4) != 0); return {}; }
    // ---- 카메라 ----
    if (cmd == "camera.on") { doc_->setCameraEnabled(true); return {}; }
    if (cmd == "camera.off") { doc_->setCameraEnabled(false); return {}; }
    if (cmd == "camera.key") {
        CameraKey k;
        k.x = num(2);
        k.y = num(3);
        k.scale = t.size() > 4 ? num(4) / 100.0 : 1.0;
        k.rotation = num(5);
        k.ease = int(num(6));
        doc_->setCameraKey(std::max(0, int(num(1)) - 1), k);
        return {};
    }
    // ---- 북마크 · 라이브러리 ----
    if (cmd == "bookmark") { doc_->toggleBookmark(t.size() > 1 ? int(num(1)) - 1 : doc_->currentFrame()); return {}; }
    if (cmd == "lib.add") {
        const int k = doc_->keyIndexAt(doc_->currentLayer(), doc_->currentFrame());
        if (k < 0) return QStringLiteral("현재 프레임에 이미지가 없습니다");
        const int id = doc_->addToLibrary(rest.isEmpty() ? QString("이미지 %1").arg(doc_->libraryCount() + 1) : rest,
                                          doc_->keyImage(doc_->currentLayer(), k));
        if (t.size() > 2) doc_->setLibraryCategory(id, t[2]);
        return {};
    }
    // ---- 저장 ----
    if (cmd == "save") { return doc_->save(rest) ? QString() : QStringLiteral("저장하지 못했습니다"); }
    if (cmd == "savecopy") { return doc_->saveCopy(rest) ? QString() : QStringLiteral("저장하지 못했습니다"); }
    if (cmd == "export.png") { return doc_->exportFrame(doc_->currentFrame(), rest) ? QString() : QStringLiteral("내보내지 못했습니다"); }
    if (cmd == "version") return QStringLiteral("ONY");
    if (host_) {
        const auto r = host_(t);
        if (r.has_value()) return *r;
    }
    return QStringLiteral("알 수 없는 명령: %1").arg(t[0]);
}

// 스크립트 콘솔 창을 만든다 (입력창·실행/도움말/열기/저장 버튼·출력창)
ScriptConsole::ScriptConsole(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    input_ = new QPlainTextEdit;
    input_->setPlaceholderText(QStringLiteral("예)\nlayer.new 배경\nrect 0 0 320 200 #88ccff\nrepeat 1 12 frame {i}"));
    output_ = new QPlainTextEdit;
    output_->setReadOnly(true);
    output_->setPlainText(ScriptEngine::help());
    auto* row = new QHBoxLayout;
    auto* run = new QPushButton(QStringLiteral("실행"));
    auto* help = new QPushButton(QStringLiteral("도움말"));
    auto* open = new QPushButton(QStringLiteral("열기"));
    auto* save = new QPushButton(QStringLiteral("저장"));
    open->setToolTip(QStringLiteral("스크립트 파일을 불러옵니다 (.onys, .txt)"));
    save->setToolTip(QStringLiteral("지금 쓴 스크립트를 파일로 저장합니다 (.onys, .txt)"));
    row->addWidget(run);
    row->addWidget(help);
    row->addWidget(open);
    row->addWidget(save);
    row->addStretch();
    static const QString kFilter = QStringLiteral("ONY 스크립트 (*.onys *.txt);;모든 파일 (*)");
    connect(open, &QPushButton::clicked, this, [this] {
        const QString p = QFileDialog::getOpenFileName(this, QStringLiteral("스크립트 열기"), QString(), kFilter);
        if (p.isEmpty()) return;
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            output_->setPlainText(QStringLiteral("열지 못했습니다: ") + p);
            return;
        }
        input_->setPlainText(QString::fromUtf8(f.readAll()));
        output_->setPlainText(QStringLiteral("불러왔습니다: ") + p);
    });
    connect(save, &QPushButton::clicked, this, [this] {
        QString p = QFileDialog::getSaveFileName(this, QStringLiteral("스크립트 저장"), QString(), kFilter);
        if (p.isEmpty()) return;
        if (!p.contains('.')) p += QStringLiteral(".onys");
        QFile f(p);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            output_->setPlainText(QStringLiteral("저장하지 못했습니다: ") + p);
            return;
        }
        f.write(input_->toPlainText().toUtf8());
        output_->setPlainText(QStringLiteral("저장했습니다: ") + p);
    });
    lay->addWidget(input_, 2);
    lay->addLayout(row);
    lay->addWidget(output_, 3);
    connect(run, &QPushButton::clicked, this, [this] {
        ScriptEngine eng(doc_);
        if (host_) eng.setHost(host_);
        output_->setPlainText(eng.run(input_->toPlainText()));
    });
    connect(help, &QPushButton::clicked, this, [this] { output_->setPlainText(ScriptEngine::help()); });
}

// ---------------------------------------------------------------- 사용자 패널
CustomPanel::CustomPanel(Document* doc, const CustomPanelDef& def, QWidget* parent)
    : QWidget(parent), doc_(doc), def_(def) {
    setMinimumSize(140, 120);
    editBox_ = new QCheckBox(QStringLiteral("배치 편집"), this);
    editBox_->setToolTip(QStringLiteral("켜면 버튼을 끌어 옮기고, 오른쪽 아래 모서리를 끌어 크기를 바꿉니다."));
    editBox_->move(6, 4);
    connect(editBox_, &QCheckBox::toggled, this, [this](bool on) { setEditing(on); });
    arrangeBtn_ = new QPushButton(QStringLiteral("자동 정렬"), this);
    arrangeBtn_->setFixedHeight(22);
    arrangeBtn_->move(6, 26);
    arrangeBtn_->setVisible(false);
    connect(arrangeBtn_, &QPushButton::clicked, this, [this] { autoArrange(); });

    for (int i = 0; i < def_.labels.size(); ++i) {
        auto* b = new QPushButton(def_.labels[i], this);
        const QString script = def_.scripts.value(i);
        Document* d = doc_;
        connect(b, &QPushButton::clicked, this, [this, d, script] {
            if (editing_) return;        // 편집 중에는 실행하지 않는다
            ScriptEngine eng(d);
            if (host_) eng.setHost(host_);
            eng.run(script);
        });
        b->installEventFilter(this);
        buttons_.push_back(b);
    }
    applyGeometry();
}

// 저장된 배치 정보대로 버튼들의 위치·크기를 적용한다
void CustomPanel::applyGeometry() {
    const int top = 52;
    for (int i = 0; i < buttons_.size(); ++i) {
        QRect r;
        const QStringList p = def_.geoms.value(i).split(',');
        if (p.size() == 4) r = QRect(p[0].toInt(), p[1].toInt(), std::max(24, p[2].toInt()), std::max(18, p[3].toInt()));
        else r = QRect(8, top + i * 30, std::max(90, width() - 16), 26);
        buttons_[i]->setGeometry(r);
        buttons_[i]->show();
    }
}

// 지금 버튼들의 위치·크기를 배치 정보로 저장한다
void CustomPanel::storeGeometry() {
    def_.geoms.clear();
    for (QPushButton* b : buttons_) {
        const QRect r = b->geometry();
        def_.geoms << QString("%1,%2,%3,%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
    }
    emit layoutChanged(def_);
}

// 버튼들을 세로로 가지런히 자동 정렬한다
void CustomPanel::autoArrange() {
    const int top = 52;
    for (int i = 0; i < buttons_.size(); ++i)
        buttons_[i]->setGeometry(8, top + i * 30, std::max(90, width() - 16), 26);
    storeGeometry();
    update();
}

// 배치 편집 모드를 켜고 끈다 (버튼 끌어서 옮기기/크기 조절 가능 여부)
void CustomPanel::setEditing(bool on) {
    editing_ = on;
    if (editBox_ && editBox_->isChecked() != on) {
        QSignalBlocker b(editBox_);
        editBox_->setChecked(on);
    }
    if (arrangeBtn_) arrangeBtn_->setVisible(on);
    for (QPushButton* b : buttons_) b->setCursor(on ? Qt::SizeAllCursor : Qt::ArrowCursor);
    update();
}

// 패널 크기가 바뀌면, 저장된 배치가 없을 때만 버튼 폭을 다시 맞춘다
void CustomPanel::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    // 저장된 배치가 없을 때만 폭을 따라간다
    if (def_.geoms.isEmpty()) applyGeometry();
}

// 편집 모드에서 버튼을 끌어 옮기거나 크기를 바꾸는 마우스 동작을 처리한다
bool CustomPanel::eventFilter(QObject* o, QEvent* e) {
    if (!editing_) return QWidget::eventFilter(o, e);
    auto* b = qobject_cast<QPushButton*>(o);
    const int idx = b ? int(buttons_.indexOf(b)) : -1;
    if (idx < 0) return QWidget::eventFilter(o, e);
    if (e->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(e);
        if (me->button() != Qt::LeftButton) return true;
        dragIdx_ = idx;
        dragStart_ = me->globalPosition().toPoint();
        dragOrig_ = b->geometry();
        // 오른쪽 아래 12px 안이면 크기 바꾸기
        resizing_ = me->position().x() > b->width() - 12 && me->position().y() > b->height() - 12;
        return true;
    }
    if (e->type() == QEvent::MouseMove && dragIdx_ == idx) {
        auto* me = static_cast<QMouseEvent*>(e);
        const QPoint d = me->globalPosition().toPoint() - dragStart_;
        if (resizing_) {
            b->setGeometry(dragOrig_.x(), dragOrig_.y(), std::max(24, dragOrig_.width() + d.x()), std::max(18, dragOrig_.height() + d.y()));
        } else {
            b->move(std::max(0, dragOrig_.x() + d.x()), std::max(0, dragOrig_.y() + d.y()));
        }
        update();
        return true;
    }
    if (e->type() == QEvent::MouseButtonRelease && dragIdx_ == idx) {
        dragIdx_ = -1;
        resizing_ = false;
        storeGeometry();
        return true;
    }
    return QWidget::eventFilter(o, e);
}

// 편집 모드일 때 버튼 둘레에 점선 테두리를 그린다
void CustomPanel::paintEvent(QPaintEvent*) {
    if (!editing_) return;
    QPainter p(this);
    p.setPen(QPen(QColor(0x4a, 0x90, 0xe2, 160), 1, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    for (QPushButton* b : buttons_) {
        p.drawRect(b->geometry().adjusted(-1, -1, 0, 0));
        const QRect g = b->geometry();
        p.fillRect(QRect(g.right() - 8, g.bottom() - 8, 8, 8), QColor(0x4a, 0x90, 0xe2, 160));
    }
}

bool editCustomPanel(QWidget* parent, CustomPanelDef* def) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("사용자 패널 만들기"));
    auto* form = new QFormLayout(&dlg);
    auto* name = new QLineEdit(def->name);
    auto* text = new QPlainTextEdit;
    text->setPlaceholderText(QStringLiteral("한 줄에 버튼 하나: 버튼이름 | 명령 (여러 명령은 ; 로 구분)\n예) 빨간 사각형 | rect 10 10 100 60 #ff0000"));
    QString init;
    for (int i = 0; i < def->labels.size(); ++i) init += def->labels[i] + " | " + def->scripts.value(i).replace('\n', ';') + "\n";
    text->setPlainText(init);
    text->setMinimumSize(420, 180);
    form->addRow(QStringLiteral("패널 이름"), name);
    form->addRow(QStringLiteral("버튼"), text);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() != QDialog::Accepted || name->text().trimmed().isEmpty()) return false;
    def->name = name->text().trimmed();
    def->labels.clear();
    def->scripts.clear();
    for (const QString& ln : text->toPlainText().split('\n')) {
        const int bar = ln.indexOf('|');
        if (bar < 0) continue;
        const QString label = ln.left(bar).trimmed();
        if (label.isEmpty()) continue;
        def->labels << label;
        def->scripts << ln.mid(bar + 1).trimmed().replace(';', '\n');
    }
    return !def->labels.isEmpty();
}

QVector<CustomPanelDef> loadCustomPanels() {
    QSettings st("Ony", "Ony");
    QVector<CustomPanelDef> out;
    const int n = st.beginReadArray("customPanels");
    for (int i = 0; i < n; ++i) {
        st.setArrayIndex(i);
        CustomPanelDef d;
        d.name = st.value("name").toString();
        d.labels = st.value("labels").toStringList();
        d.geoms = st.value("geoms").toStringList();
        d.scripts = st.value("scripts").toStringList();
        if (!d.name.isEmpty()) out.push_back(d);
    }
    st.endArray();
    return out;
}

void saveCustomPanels(const QVector<CustomPanelDef>& defs) {
    QSettings st("Ony", "Ony");
    st.beginWriteArray("customPanels", defs.size());
    for (int i = 0; i < defs.size(); ++i) {
        st.setArrayIndex(i);
        st.setValue("name", defs[i].name);
        st.setValue("labels", defs[i].labels);
        st.setValue("geoms", defs[i].geoms);
        st.setValue("scripts", defs[i].scripts);
    }
    st.endArray();
}

// ---------------------------------------------------------------- 스토리보드
StoryboardPanel::StoryboardPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    list_ = new QListWidget;
    list_->setViewMode(QListView::IconMode);
    list_->setIconSize(QSize(160, 90));
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setWordWrap(true);
    list_->setSpacing(6);
    lay->addWidget(new QLabel(QStringLiteral("컷(이미지가 바뀌는 프레임). 더블클릭: 메모 편집")));
    lay->addWidget(list_, 1);
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) { doc_->setCurrentFrame(it->data(Qt::UserRole).toInt()); });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        const int f = it->data(Qt::UserRole).toInt();
        bool ok = false;
        const QString t = QInputDialog::getMultiLineText(this, QStringLiteral("메모"), QString("프레임 %1 메모:").arg(f + 1), doc_->note(f), &ok);
        if (ok) doc_->setNote(f, t);
    });
    connect(doc_, &Document::structureChanged, this, &StoryboardPanel::rebuild);
    connect(doc_, &Document::libraryChanged, this, &StoryboardPanel::rebuild);
    rebuild();
}

// 그림이 있거나 메모가 있는 프레임들을 모아 스토리보드 목록을 다시 만든다
void StoryboardPanel::rebuild() {
    list_->clear();
    QSet<int> starts;
    for (int l = 0; l < doc_->layerCount(); ++l)
        for (int f = 0; f < doc_->frameCount(); ++f)
            if (!doc_->layer(l).frames[f].isNull()) starts.insert(f);
    for (auto it = doc_->notes().cbegin(); it != doc_->notes().cend(); ++it) starts.insert(it.key());
    QList<int> sorted = starts.values();
    std::sort(sorted.begin(), sorted.end());
    if (sorted.isEmpty()) sorted << 0;
    for (int f : sorted) {
        const QImage thumb = doc_->composite(f).scaled(160, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(thumb)), QString("%1\n%2").arg(f + 1).arg(doc_->note(f)));
        item->setData(Qt::UserRole, f);
        list_->addItem(item);
    }
}

// ---------------------------------------------------------------- 뼈대(본) 변형
BoneWarpDialog::BoneWarpDialog(const QImage& img, QWidget* parent) : QDialog(parent), img_(img) {
    setWindowTitle(QStringLiteral("뼈대 변형 — 빈 곳을 끌어 뼈를 놓고, 뼈 끝을 끌어 휘게 합니다"));
    resize(920, 640);
    setMouseTracking(true);
    auto* bar = new QWidget(this);
    auto* bh = new QHBoxLayout(bar);
    bh->setContentsMargins(0, 0, 0, 0);
    bh->addWidget(new QLabel(QStringLiteral("영향 범위")));
    radiusBox_ = new QSpinBox;
    radiusBox_->setRange(10, 800);
    radiusBox_->setValue(int(radius_));
    radiusBox_->setSuffix(QStringLiteral(" px"));
    radiusBox_->setToolTip(QStringLiteral("새로 놓는 뼈가 끌어당길 범위입니다. 고른 뼈가 있으면 그 뼈의 범위를 바꿉니다."));
    bh->addWidget(radiusBox_);
    auto* undo = new QPushButton(QStringLiteral("뼈 하나 지우기"));
    auto* clear = new QPushButton(QStringLiteral("모두 지우기"));
    auto* reset = new QPushButton(QStringLiteral("자세 되돌리기"));
    auto* orig = new QPushButton(QStringLiteral("원본 보기"));
    orig->setCheckable(true);
    for (QPushButton* b : {undo, clear, reset, orig}) bh->addWidget(b);
    auto* follow = new QCheckBox(QStringLiteral("범위 밖도 따라오기"));
    follow->setToolTip(QStringLiteral("켜면 뼈 범위를 벗어난 곳도 가장 가까운 뼈를 따라갑니다. 그림이 찢어지지 않습니다."));
    follow->setChecked(followNearest_);
    bh->addWidget(follow);
    connect(follow, &QCheckBox::toggled, this, [this](bool on) {
        followNearest_ = on;
        preview_ = QImage();
        update();
    });
    bh->addStretch();
    bar->move(20, 6);
    bar->resize(640, 28);
    connect(radiusBox_, &QSpinBox::valueChanged, this, [this](int v) {
        radius_ = v;
        if (drag_ >= 0 && drag_ < bones_.size()) {
            bones_[drag_].radius = v;
            preview_ = QImage();
        }
        update();
    });
    connect(undo, &QPushButton::clicked, this, [this] {
        if (!bones_.isEmpty()) bones_.removeLast();
        preview_ = QImage();
        update();
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        bones_.clear();
        preview_ = QImage();
        update();
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        for (Warp::Bone& b : bones_) {
            b.da = b.a;
            b.db = b.b;
        }
        preview_ = QImage();
        update();
    });
    connect(orig, &QPushButton::toggled, this, [this](bool on) {
        showOriginal_ = on;
        update();
    });
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    bb->move(width() - 200, height() - 40);
    bb->setFixedWidth(190);
    bb->raise();
}

// 지금까지 놓은 뼈대로 변형한 최종 이미지를 계산한다
QImage BoneWarpDialog::result() const { return Warp::boneWarp(img_, bones_, followNearest_); }

// 그림이 패널 안에서 차지할 사각형을 계산한다 (비율 유지, 가운데 정렬)
QRectF BoneWarpDialog::imageRect() const {
    const QRectF area(20, 44, width() - 40, height() - 100);
    const double s = std::min(area.width() / img_.width(), area.height() / img_.height());
    const QSizeF sz(img_.width() * s, img_.height() * s);
    return QRectF(area.center().x() - sz.width() / 2, area.center().y() - sz.height() / 2, sz.width(), sz.height());
}
// 이미지 좌표를 위젯 화면 좌표로 변환한다
QPointF BoneWarpDialog::toWidget(const QPointF& i) const {
    const QRectF r = imageRect();
    return QPointF(r.x() + i.x() * r.width() / img_.width(), r.y() + i.y() * r.height() / img_.height());
}
// 위젯 화면 좌표를 이미지 좌표로 변환한다
QPointF BoneWarpDialog::toImage(const QPointF& w) const {
    const QRectF r = imageRect();
    return QPointF((w.x() - r.x()) * img_.width() / r.width(), (w.y() - r.y()) * img_.height() / r.height());
}

// 화면 좌표에서 가까운 뼈의 손잡이(시작/끝점)를 찾는다
int BoneWarpDialog::pickHandle(const QPointF& wp, bool* isEnd) const {
    for (int i = 0; i < bones_.size(); ++i) {
        if (QLineF(toWidget(bones_[i].da), wp).length() < 10) {
            *isEnd = false;
            return i;
        }
        if (QLineF(toWidget(bones_[i].db), wp).length() < 10) {
            *isEnd = true;
            return i;
        }
    }
    return -1;
}

// 마우스 누름: 기존 손잡이를 잡거나, 빈 곳이면 새 뼈 놓기를 시작한다
void BoneWarpDialog::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const QPointF wp = e->position();
    if (!imageRect().contains(wp)) return;
    bool isEnd = false;
    const int i = pickHandle(wp, &isEnd);
    if (i >= 0) {
        drag_ = i;
        dragEnd_ = isEnd;
        QSignalBlocker b(radiusBox_);
        radiusBox_->setValue(int(bones_[i].radius));
        return;
    }
    placing_ = true;
    newStart_ = toImage(wp);
    hover_ = newStart_;
    update();
}

// 마우스 이동: 손잡이를 드래그 중이면 뼈 끝점을 따라 옮긴다
void BoneWarpDialog::mouseMoveEvent(QMouseEvent* e) {
    const QPointF wp = e->position();
    hover_ = toImage(wp);
    if (drag_ >= 0 && drag_ < bones_.size()) {
        (dragEnd_ ? bones_[drag_].db : bones_[drag_].da) = hover_;
        preview_ = QImage();
    }
    if (drag_ >= 0 || placing_) update();
}

// 마우스 뗌: 새 뼈 놓기를 끝내고 (너무 짧지 않으면) 뼈 목록에 추가한다
void BoneWarpDialog::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    if (placing_) {
        placing_ = false;
        const QPointF end = toImage(e->position());
        if (QLineF(newStart_, end).length() > 6) {
            Warp::Bone b;
            b.a = b.da = newStart_;
            b.b = b.db = end;
            b.radius = radius_;
            bones_.push_back(b);
            preview_ = QImage();
        }
    }
    drag_ = -1;
    update();
}

// 그림(원본 또는 변형 미리보기)과 뼈대·영향 범위를 그린다
void BoneWarpDialog::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().window());
    const QRectF r = imageRect();
    p.fillRect(r, Qt::white);
    if (showOriginal_ || bones_.isEmpty()) {
        p.drawImage(r, img_);
    } else {
        if (preview_.isNull() || preview_.size() != img_.size()) preview_ = result();
        p.drawImage(r, preview_);
    }
    // 뼈 그리기
    for (int i = 0; i < bones_.size(); ++i) {
        const Warp::Bone& b = bones_[i];
        const QPointF a0 = toWidget(b.a), b0 = toWidget(b.b);
        const QPointF a1 = toWidget(b.da), b1 = toWidget(b.db);
        // 처음 자세는 흐리게
        p.setPen(QPen(QColor(160, 160, 160, 120), 1, Qt::DashLine));
        p.drawLine(a0, b0);
        // 지금 자세
        p.setPen(QPen(QColor(0xff, 0xaa, 0x33), 3));
        p.drawLine(a1, b1);
        p.setBrush(Qt::white);
        p.setPen(QPen(QColor(0x40, 0x40, 0x40), 1));
        p.drawEllipse(a1, 5, 5);
        p.drawEllipse(b1, 5, 5);
        // 영향 범위
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0xff, 0xaa, 0x33, 60), 1, Qt::DotLine));
        const double rr = b.radius * r.width() / img_.width();
        p.drawEllipse(a1, rr, rr);
        p.drawEllipse(b1, rr, rr);
    }
    if (placing_) {
        p.setPen(QPen(QColor(0x4a, 0x90, 0xe2), 2, Qt::DashLine));
        p.drawLine(toWidget(newStart_), toWidget(hover_));
    }
    if (bones_.isEmpty()) {
        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(r.adjusted(10, 10, -10, -10), Qt::AlignTop | Qt::AlignHCenter,
                   QStringLiteral("그림 위를 끌어 뼈를 놓으세요. 놓은 뒤 양 끝 동그라미를 끌면 그림이 따라 휩니다."));
    }
}

// ---------------------------------------------------------------- 메시 워프
MeshWarpDialog::MeshWarpDialog(const QImage& img, QWidget* parent) : QDialog(parent), img_(img) {
    setWindowTitle(QStringLiteral("메시 워프 — 격자점을 끌어 변형 (더블클릭: 초기화)"));
    resize(900, 620);
    for (int r = 0; r <= rows_; ++r)
        for (int c = 0; c <= cols_; ++c) pts_.push_back(QPointF(c * img_.width() / double(cols_), r * img_.height() / double(rows_)));
    auto* bar = new QWidget(this);
    auto* bh = new QHBoxLayout(bar);
    bh->setContentsMargins(0, 0, 0, 0);
    bh->addWidget(new QLabel(QStringLiteral("격자")));
    colBox_ = new QSpinBox;
    colBox_->setRange(1, 24);
    colBox_->setValue(cols_);
    colBox_->setPrefix(QStringLiteral("가로 "));
    rowBox_ = new QSpinBox;
    rowBox_->setRange(1, 24);
    rowBox_->setValue(rows_);
    rowBox_->setPrefix(QStringLiteral("세로 "));
    bh->addWidget(colBox_);
    bh->addWidget(rowBox_);
    auto* reset = new QPushButton(QStringLiteral("점 되돌리기"));
    bh->addWidget(reset);
    auto* orig = new QPushButton(QStringLiteral("원본 보기"));
    orig->setCheckable(true);
    bh->addWidget(orig);
    bh->addStretch();
    bar->move(20, 6);
    bar->resize(520, 28);
    connect(colBox_, &QSpinBox::valueChanged, this, [this](int v) { setGrid(v, rows_); });
    connect(rowBox_, &QSpinBox::valueChanged, this, [this](int v) { setGrid(cols_, v); });
    connect(reset, &QPushButton::clicked, this, [this] { resetPoints(); });
    connect(orig, &QPushButton::toggled, this, [this](bool on) {
        showOriginal_ = on;
        update();
    });

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    bb->move(width() - 200, height() - 40);
    bb->setFixedWidth(190);
    bb->raise();
}

// 격자점들을 모두 초기 위치로 되돌린다
void MeshWarpDialog::resetPoints() {
    pts_.clear();
    for (int r = 0; r <= rows_; ++r)
        for (int c = 0; c <= cols_; ++c) pts_.push_back(QPointF(c * img_.width() / double(cols_), r * img_.height() / double(rows_)));
    preview_ = QImage();
    update();
}

// 격자 칸 수를 바꾼다 (기존에 옮겨둔 변형은 비율로 최대한 유지한다)
void MeshWarpDialog::setGrid(int cols, int rows) {
    cols = std::clamp(cols, 1, 24);
    rows = std::clamp(rows, 1, 24);
    if (cols == cols_ && rows == rows_) return;
    // 이미 옮긴 모양을 최대한 유지한다: 지금 격자에서 같은 비율 위치의 변형량을 새 격자에 옮겨 담는다
    const QVector<QPointF> old = pts_;
    const int oc = cols_, orow = rows_;
    cols_ = cols;
    rows_ = rows;
    pts_.clear();
    for (int r = 0; r <= rows_; ++r) {
        for (int c = 0; c <= cols_; ++c) {
            const double u = double(c) / cols_, v = double(r) / rows_;
            const QPointF base(u * img_.width(), v * img_.height());
            // 옛 격자에서 (u,v) 위치의 변형량을 쌍선형으로 읽는다
            const double fx = u * oc, fy = v * orow;
            const int x0 = std::clamp(int(fx), 0, oc), y0 = std::clamp(int(fy), 0, orow);
            const int x1 = std::min(x0 + 1, oc), y1 = std::min(y0 + 1, orow);
            const double tx = fx - x0, ty = fy - y0;
            auto at = [&](int cx, int cy) {
                const int idx = cy * (oc + 1) + cx;
                if (idx < 0 || idx >= old.size()) return QPointF(0, 0);
                return old[idx] - QPointF(double(cx) / oc * img_.width(), double(cy) / orow * img_.height());
            };
            const QPointF d = at(x0, y0) * (1 - tx) * (1 - ty) + at(x1, y0) * tx * (1 - ty) +
                              at(x0, y1) * (1 - tx) * ty + at(x1, y1) * tx * ty;
            pts_.push_back(base + d);
        }
    }
    if (colBox_ && colBox_->value() != cols_) {
        QSignalBlocker b(colBox_);
        colBox_->setValue(cols_);
    }
    if (rowBox_ && rowBox_->value() != rows_) {
        QSignalBlocker b(rowBox_);
        rowBox_->setValue(rows_);
    }
    preview_ = QImage();
    update();
}

// 그림이 패널 안에서 차지할 사각형을 계산한다 (비율 유지, 가운데 정렬)
QRectF MeshWarpDialog::imageRect() const {
    const QRectF area(20, 20, width() - 40, height() - 80);
    const double s = std::min(area.width() / img_.width(), area.height() / img_.height());
    const QSizeF sz(img_.width() * s, img_.height() * s);
    return QRectF(area.center().x() - sz.width() / 2, area.center().y() - sz.height() / 2, sz.width(), sz.height());
}
// 이미지 좌표를 위젯 화면 좌표로 변환한다
QPointF MeshWarpDialog::toWidget(const QPointF& i) const {
    const QRectF r = imageRect();
    return QPointF(r.x() + i.x() * r.width() / img_.width(), r.y() + i.y() * r.height() / img_.height());
}
// 위젯 화면 좌표를 이미지 좌표로 변환한다
QPointF MeshWarpDialog::toImage(const QPointF& w) const {
    const QRectF r = imageRect();
    return QPointF((w.x() - r.x()) * img_.width() / r.width(), (w.y() - r.y()) * img_.height() / r.height());
}

// 지금까지 옮긴 격자점으로 변형한 최종 이미지를 계산한다
QImage MeshWarpDialog::result() const { return Warp::meshWarp(img_, cols_, rows_, pts_); }

// 그림(원본 또는 변형 미리보기)과 격자선·격자점을 그린다
void MeshWarpDialog::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().window());
    const QRectF r = imageRect();
    p.fillRect(r, Qt::white);
    if (showOriginal_) {
        p.drawImage(r, img_);
    } else {
        if (preview_.isNull() || preview_.size() != img_.size()) preview_ = result();
        p.drawImage(r, preview_);
    }
    p.setPen(QColor(0x4a, 0x90, 0xe2));
    for (int rr = 0; rr <= rows_; ++rr)
        for (int c = 0; c <= cols_; ++c) {
            const QPointF a = toWidget(pts_[rr * (cols_ + 1) + c]);
            if (c < cols_) p.drawLine(a, toWidget(pts_[rr * (cols_ + 1) + c + 1]));
            if (rr < rows_) p.drawLine(a, toWidget(pts_[(rr + 1) * (cols_ + 1) + c]));
        }
    p.setBrush(Qt::white);
    for (const QPointF& pt : pts_) p.drawEllipse(toWidget(pt), 5, 5);
}

// 마우스 누름: 가장 가까운 격자점을 찾아 드래그 대상으로 잡는다
void MeshWarpDialog::mousePressEvent(QMouseEvent* e) {
    drag_ = -1;
    double best = 144;
    for (int i = 0; i < pts_.size(); ++i) {
        const QPointF d = toWidget(pts_[i]) - e->position();
        const double dd = d.x() * d.x() + d.y() * d.y();
        if (dd < best) { best = dd; drag_ = i; }
    }
}
// 마우스 이동: 잡은 격자점을 마우스 위치로 옮긴다
void MeshWarpDialog::mouseMoveEvent(QMouseEvent* e) {
    if (drag_ < 0) return;
    pts_[drag_] = toImage(e->position());
    update();
}
// 마우스 뗌: 드래그를 끝내고 미리보기를 다시 계산한다
void MeshWarpDialog::mouseReleaseEvent(QMouseEvent*) {
    if (drag_ >= 0) preview_ = result();
    drag_ = -1;
    update();
}
