#include "License.h"

#include <QCheckBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMessageAuthenticationCode>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QSysInfo>
#include <QUuid>
#include <QDateTime>

namespace License {

namespace {
QSettings& settings() {
    static QSettings s("Ony", "OnyLicense");
    return s;
}
// 16진 문자열을 바이트로 바꾼다 (비밀키·서명 비교용)
QByteArray hexToBytes(const QByteArray& hex) { return QByteArray::fromHex(hex); }
}

// 이 컴퓨터를 나타내는 고유 ID. QSysInfo::machineUniqueId() 가 비어 있는 환경(일부 테스트/샌드박스)에서는
// 한 번 만든 UUID를 저장해 두고 그걸 계속 쓴다. 어느 쪽이든 해시해서 저장하므로 원래 값은 남지 않는다.
QString machineId() {
    QByteArray raw = QSysInfo::machineUniqueId();
    if (raw.isEmpty()) {
        QString saved = settings().value("license/fallbackMachineUuid").toString();
        if (saved.isEmpty()) {
            saved = QUuid::createUuid().toString(QUuid::WithoutBraces);
            settings().setValue("license/fallbackMachineUuid", saved);
        }
        raw = saved.toUtf8();
    }
    return QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex());
}

QByteArray hmacHex(const QByteArray& canonical, const QByteArray& secretHex) {
    QMessageAuthenticationCode mac(QCryptographicHash::Sha256);
    mac.setKey(hexToBytes(secretHex));
    mac.addData(canonical);
    return mac.result().toHex();
}

// 서버 응답을 서명과 함께 재구성하기 위한 고정 순서 문자열. 서버(JS)와 반드시 같은 순서·구분자를 써야 한다.
static QByteArray canonicalOf(const QString& licenseKey, const QString& machine, const QString& plan,
                               const QString& issuedAt, const QString& expiresAt) {
    return QString("%1|%2|%3|%4|%5").arg(licenseKey, machine, plan, issuedAt, expiresAt).toUtf8();
}

static bool verifyStoredSignature(QSettings& s, QString* why) {
    const QString key = s.value("license/key").toString();
    if (key.isEmpty()) { if (why) *why = QStringLiteral("등록된 라이선스가 없습니다."); return false; }
    const QString plan = s.value("license/plan").toString();
    const QString issuedAt = s.value("license/issuedAt").toString();
    const QString expiresAt = s.value("license/expiresAt").toString();
    const QString sig = s.value("license/sig").toString();
    const QString machine = s.value("license/machineId").toString();
    if (machine != machineId()) { if (why) *why = QStringLiteral("다른 기기에서 저장된 라이선스입니다."); return false; }
    const QByteArray expect = hmacHex(canonicalOf(key, machine, plan, issuedAt, expiresAt), kSharedSecretHex);
    if (sig.toUtf8() != expect) { if (why) *why = QStringLiteral("저장된 라이선스 서명이 올바르지 않습니다."); return false; }
    return true;
}

Status currentStatus() {
    Status st;
    QSettings& s = settings();
    st.licenseKey = s.value("license/key").toString();
    st.knownBad = s.value("license/knownBad", false).toBool();
    if (st.licenseKey.isEmpty()) {
        st.message = QStringLiteral("아직 라이선스가 등록되지 않았습니다.");
        return st;
    }
    QString why;
    if (!verifyStoredSignature(s, &why)) {
        st.message = why;
        return st;
    }
    st.plan = s.value("license/plan").toString();
    st.expiresAt = QDateTime::fromString(s.value("license/expiresAt").toString(), Qt::ISODate);
    st.contactConsentGiven = s.value("license/consentGiven", false).toBool();
    st.contactEmail = s.value("license/contactEmail").toString();
    if (st.knownBad) {
        st.message = QStringLiteral("서버에서 이 라이선스를 취소/만료 처리했습니다.");
        return st;
    }
    if (!st.expiresAt.isValid() || QDateTime::currentDateTimeUtc() > st.expiresAt) {
        st.message = QStringLiteral("오프라인 유효기간이 지났습니다. 인터넷에 연결한 뒤 다시 확인해 주세요.");
        return st;
    }
    st.licensed = true;
    st.message = QString("플랜: %1  (다음 서버 확인: %2)").arg(st.plan.isEmpty() ? QStringLiteral("standard") : st.plan,
                                                           st.expiresAt.toLocalTime().toString("yyyy-MM-dd HH:mm"));
    return st;
}

void clearLicense() {
    QSettings& s = settings();
    for (const char* k : {"license/key", "license/plan", "license/issuedAt", "license/expiresAt", "license/sig",
                          "license/machineId", "license/knownBad", "license/consentGiven", "license/contactEmail"})
        s.remove(k);
}

QString serverUrl() {
    const QString v = settings().value("license/serverUrl").toString();
    return v.isEmpty() ? QString::fromLatin1(kDefaultServerUrl) : v;
}
void setServerUrl(const QString& url) { settings().setValue("license/serverUrl", url); }

// 서버가 돌려주는 오류 코드를 사람이 읽을 설명으로 바꾼다.
static QString friendlyError(const QString& code) {
    static const QMap<QString, QString> kErrText{
        {"not_found", QStringLiteral("존재하지 않는 라이선스 키입니다.")},
        {"revoked", QStringLiteral("이 라이선스는 취소되었습니다.")},
        {"expired", QStringLiteral("이 라이선스는 기간이 만료되었습니다.")},
        {"machine_limit_reached", QStringLiteral("활성화 가능한 기기 수를 넘었습니다. 다른 기기에서 먼저 해제해 주세요.")},
        {"not_activated", QStringLiteral("이 기기에서 먼저 활성화가 필요합니다.")},
    };
    return kErrText.value(code, code.isEmpty() ? QStringLiteral("알 수 없는 오류입니다.") : code);
}

// 생성자: 이 안에서만 쓰는 QNetworkAccessManager 를 만든다
Client::Client(QObject* parent) : QObject(parent), nam_(new QNetworkAccessManager(this)) {}

// 서버가 돌려준 발급 내용(라이선스/기기/플랜/시각)의 서명을 검증하고, 맞으면 로컬에 저장한다
bool Client::applyGrant(const QJsonObject& obj, QString* err) {
    const QString key = obj.value("license_key").toString();
    const QString machine = obj.value("machine_id").toString();
    const QString plan = obj.value("plan").toString();
    const QString issuedAt = obj.value("issued_at").toString();
    const QString expiresAt = obj.value("expires_at").toString();
    const QString sig = obj.value("sig").toString();
    if (machine != machineId()) { if (err) *err = QStringLiteral("이 기기의 ID와 서버 응답이 일치하지 않습니다."); return false; }
    const QByteArray expect = hmacHex(canonicalOf(key, machine, plan, issuedAt, expiresAt), kSharedSecretHex);
    if (sig.toUtf8() != expect) { if (err) *err = QStringLiteral("서버 응답의 서명이 올바르지 않습니다 (중간에서 바뀌었을 수 있습니다)."); return false; }
    QSettings& s = settings();
    s.setValue("license/key", key);
    s.setValue("license/machineId", machine);
    s.setValue("license/plan", plan);
    s.setValue("license/issuedAt", issuedAt);
    s.setValue("license/expiresAt", expiresAt);
    s.setValue("license/sig", sig);
    s.setValue("license/knownBad", false);
    return true;
}

// 서버에 라이선스 키를 보내 이 기기에서 활성화한다. contactEmail/consent 는 동의한 경우에만 같이 보낸다.
void Client::activate(const QString& licenseKey, const QString& contactEmail, bool consent) {
    const bool sendContact = consent && !contactEmail.trimmed().isEmpty();
    QJsonObject body{{"license_key", licenseKey}, {"machine_id", machineId()}};
    if (sendContact) {
        body["contact_email"] = contactEmail.trimmed();
        body["consent"] = true;
    }
    QNetworkRequest req(QUrl(serverUrl() + "/activate"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = nam_->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, contactEmail, sendContact] {
        reply->deleteLater();
        const QByteArray raw = reply->readAll();
        if (reply->error() != QNetworkReply::NoError && raw.isEmpty()) {
            emit activated(false, QStringLiteral("서버에 연결하지 못했습니다: %1").arg(reply->errorString()));
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(raw).object();
        if (!obj.value("ok").toBool()) {
            emit activated(false, friendlyError(obj.value("error").toString()));
            return;
        }
        QString err;
        if (!applyGrant(obj, &err)) { emit activated(false, err); return; }
        // 서버에 보낸 동의 내용을 로컬에도 echo 해 둔다 (라이선스 정보 화면에서 보여주기용).
        settings().setValue("license/consentGiven", sendContact);
        settings().setValue("license/contactEmail", sendContact ? contactEmail.trimmed() : QString());
        emit activated(true, QStringLiteral("활성화되었습니다."));
    });
}

// 저장된 키로 서버에 다시 확인한다 (성공하면 오프라인 유효기간이 새로 갱신된다)
void Client::revalidate() {
    const QString key = settings().value("license/key").toString();
    if (key.isEmpty()) { emit revalidated(false, QStringLiteral("등록된 라이선스가 없습니다.")); return; }
    const QJsonObject body{{"license_key", key}, {"machine_id", machineId()}};
    QNetworkRequest req(QUrl(serverUrl() + "/validate"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = nam_->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QByteArray raw = reply->readAll();
        if (reply->error() != QNetworkReply::NoError && raw.isEmpty()) {
            // 네트워크가 안 될 뿐이면 캐시된 서명으로 계속 쓰게 둔다 (knownBad 는 손대지 않음).
            emit revalidated(false, QStringLiteral("서버에 연결하지 못했습니다 (오프라인 상태로 계속 사용됩니다): %1").arg(reply->errorString()));
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(raw).object();
        if (!obj.value("ok").toBool()) {
            const QString code = obj.value("error").toString();
            if (code == QStringLiteral("revoked") || code == QStringLiteral("expired") || code == QStringLiteral("not_activated"))
                settings().setValue("license/knownBad", true); // 서버가 분명히 안 된다고 했으니 캐시도 더는 믿지 않는다
            emit revalidated(false, friendlyError(code));
            return;
        }
        QString err;
        if (!applyGrant(obj, &err)) { emit revalidated(false, err); return; }
        emit revalidated(true, QStringLiteral("다시 확인했습니다."));
    });
}

// 이 기기에서 라이선스를 반납한다 (다른 기기에서 쓸 수 있게 자리를 비운다)
void Client::deactivate() {
    const QString key = settings().value("license/key").toString();
    if (key.isEmpty()) { emit deactivated(true, QString()); return; }
    const QJsonObject body{{"license_key", key}, {"machine_id", machineId()}};
    QNetworkRequest req(QUrl(serverUrl() + "/deactivate"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = nam_->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply] {
        reply->deleteLater();
        clearLicense();
    });
    emit deactivated(true, QStringLiteral("이 기기에서 라이선스를 반납했습니다."));
}

// 이미 보낸 연락처 이메일/동의 기록을 서버에서 지워 달라고 요청한다 (라이선스 활성화 자체는 그대로 둔다)
void Client::withdrawContact() {
    const QString key = settings().value("license/key").toString();
    if (key.isEmpty()) { emit contactWithdrawn(true, QString()); return; }
    const QJsonObject body{{"license_key", key}, {"machine_id", machineId()}};
    QNetworkRequest req(QUrl(serverUrl() + "/withdraw-contact"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = nam_->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit contactWithdrawn(false, QStringLiteral("서버에 연결하지 못했습니다: %1").arg(reply->errorString()));
            return;
        }
        settings().remove("license/consentGiven");
        settings().remove("license/contactEmail");
        emit contactWithdrawn(true, QStringLiteral("연락처 정보를 삭제했습니다."));
    });
}

bool ensureLicensed(QWidget* parent) {
    Status st = currentStatus();
    if (st.licensed) {
        // 만료가 며칠 안 남았으면 뒤에서 조용히 한 번 더 확인해 둔다 (실패해도 캐시로 계속 쓰면 되므로 무시).
        if (st.expiresAt.isValid() && QDateTime::currentDateTimeUtc().addDays(3) > st.expiresAt) {
            auto* bg = new Client(parent);
            QObject::connect(bg, &Client::revalidated, bg, [bg](bool, const QString&) { bg->deleteLater(); });
            bg->revalidate();
        }
        return true;
    }

    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("ONY 라이선스"));
    dlg.setMinimumWidth(420);
    auto* lay = new QVBoxLayout(&dlg);
    auto* info = new QLabel(st.knownBad
        ? QStringLiteral("이 라이선스는 서버에서 취소/만료 처리되었습니다. 다른 키를 입력해 주세요.")
        : QStringLiteral("ONY 를 쓰려면 라이선스 키를 입력해 주세요.\n(인터넷에 연결되어 있어야 합니다.)"));
    info->setWordWrap(true);
    lay->addWidget(info);
    auto* form = new QFormLayout;
    auto* keyEdit = new QLineEdit(st.licenseKey);
    keyEdit->setPlaceholderText("ONY-XXXX-XXXX-XXXX-XXXX");
    form->addRow("라이선스 키", keyEdit);
    lay->addLayout(form);

    auto* consentBox = new QCheckBox(QStringLiteral("연락처 수집에 동의합니다 (선택)"));
    auto* emailEdit = new QLineEdit(st.contactEmail);
    emailEdit->setPlaceholderText("you@example.com");
    emailEdit->setEnabled(false);
    auto* consentNote = new QLabel(QStringLiteral(
        "이 라이선스가 무단으로 여러 곳에서 쓰이는 것으로 의심될 때 연락드리기 위한 목적으로만 이메일을 씁니다.\n"
        "동의하지 않아도 ONY 사용에는 영향이 없고, 나중에 도움말 → 라이선스 정보에서 언제든 삭제를 요청할 수 있습니다."));
    consentNote->setWordWrap(true);
    consentNote->setStyleSheet("color:#a0a0a0; font-size:11px;");
    lay->addWidget(consentBox);
    lay->addWidget(emailEdit);
    lay->addWidget(consentNote);
    QObject::connect(consentBox, &QCheckBox::toggled, emailEdit, &QLineEdit::setEnabled);
    if (st.contactConsentGiven) consentBox->setChecked(true);

    auto* status = new QLabel;
    status->setWordWrap(true);
    status->setStyleSheet("color:#e2694a;");
    lay->addWidget(status);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText("활성화");
    bb->button(QDialogButtonBox::Cancel)->setText("종료");
    lay->addWidget(bb);

    auto* client = new Client(&dlg);
    bool busy = false;
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, [&] {
        if (busy) return;
        const QString key = keyEdit->text().trimmed();
        if (key.isEmpty()) { status->setText("키를 입력해 주세요."); return; }
        busy = true;
        bb->button(QDialogButtonBox::Ok)->setEnabled(false);
        status->setStyleSheet("color:#a0a0a0;");
        status->setText("서버에 확인하는 중...");
        client->activate(key, emailEdit->text(), consentBox->isChecked());
    });
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(client, &Client::activated, &dlg, [&](bool ok, const QString& msg) {
        busy = false;
        bb->button(QDialogButtonBox::Ok)->setEnabled(true);
        if (ok) { dlg.accept(); return; }
        status->setStyleSheet("color:#e2694a;");
        status->setText(msg);
    });

    return dlg.exec() == QDialog::Accepted;
}

} // namespace License
