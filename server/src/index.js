// ONY 라이선스 서버 (Cloudflare Workers + D1 + KV).
//
// 엔드포인트
//   POST /activate    {license_key, machine_id}  -> 이 기기에 처음 등록
//   POST /validate     {license_key, machine_id}  -> 이미 등록된 기기가 다시 확인 (주기적 재검증)
//   POST /deactivate    {license_key, machine_id}  -> 이 기기에서 자리 반납
//   POST /withdraw-contact {license_key, machine_id}  -> 저장된 연락처 이메일/동의만 삭제 (활성화는 유지)
//   POST /admin/create   {email?, plan?, max_activations?, expires_at?}  헤더 x-admin-key 필요
//   POST /admin/revoke    {license_key}                                  헤더 x-admin-key 필요
//   POST /admin/unrevoke  {license_key}                                  헤더 x-admin-key 필요
//   GET  /admin/license?key=...                                          헤더 x-admin-key 필요
//
// 모든 응답은 JSON. 성공한 발급(activate/validate)은 ONY 클라이언트(src/License.cpp)와 똑같은 공식으로
// 만든 HMAC-SHA256 서명(sig)을 같이 내려준다:
//   서명 대상 문자열 = `${license_key}|${machine_id}|${plan}|${issued_at}|${expires_at}`
//   서명 = HMAC-SHA256(SECRET, 그 문자열) 의 16진 표현 (소문자)
// 이 서명이 있어야 클라이언트가 네트워크 없이도 "이 발급 내용이 진짜 서버가 준 것"임을 확인할 수 있다.

const TOKEN_TTL_DAYS = 14; // 한 번 발급받으면 이 기간은 오프라인으로 버틸 수 있다
const NEG_CACHE_TTL_SEC = 30; // KV 에 "취소/만료/없음" 결과를 잠깐 캐시해 D1 읍답 줌

function nowIso() { return new Date().toISOString(); }
function addDaysIso(days) { return new Date(Date.now() + days * 86400000).toISOString(); }

function hexToBytes(hex) {
  const out = new Uint8Array(hex.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(hex.substr(i * 2, 2), 16);
  return out;
}
function bytesToHex(buf) {
  return Array.from(new Uint8Array(buf)).map((b) => b.toString(16).padStart(2, "0")).join("");
}
async function hmacHex(secretHex, text) {
  const key = await crypto.subtle.importKey("raw", hexToBytes(secretHex), { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
  const sig = await crypto.subtle.sign("HMAC", key, new TextEncoder().encode(text));
  return bytesToHex(sig);
}
function canonical(licenseKey, machineId, plan, issuedAt, expiresAt) {
  return `${licenseKey}|${machineId}|${plan}|${issuedAt}|${expiresAt}`;
}

// 브라우저에서 만든 관리용 페이지(admin.html)가 이 Worker를 바로 호출할 수 있도록 CORS 허용.
// admin.html 은 관리자 키를 직접 입력해 쓰는 개인용 도구라, 모든 출처(*)를 허용해도 보안에 문제는
// x-admin-key 값 자체를 아는 사람만 admin/* 를 쓸 수 있다는 데서 나온다 (헤더 자체는 아무나 못 봄).
const CORS_HEADERS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
  "Access-Control-Allow-Headers": "content-type, x-admin-key",
};

function json(obj, status = 200) {
  return new Response(JSON.stringify(obj), { status, headers: { "content-type": "application/json; charset=utf-8", ...CORS_HEADERS } });
}
const fail = (error, status = 400) => json({ ok: false, error }, status);

function randomKey() {
  // ONY-XXXX-XXXX-XXXX-XXXX 형태. 사람이 눈으로 구분하기 쉬운 문자만 쓴다 (0/O, 1/I 혼동 방지).
  const alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  const group = () => Array.from({ length: 4 }, () => alphabet[Math.floor(Math.random() * alphabet.length)]).join("");
  return `ONY-${group()}-${group()}-${group()}-${group()}`;
}

async function readJson(req) {
  try { return await req.json(); } catch { return null; }
}

// 라이선스 한 건을 찾아 상태를 검사한다. 활성화 테이블까지 같이 본다.
async function loadLicense(db, licenseKey) {
  const row = await db.prepare("SELECT * FROM licenses WHERE license_key = ?").bind(licenseKey).first();
  return row || null;
}

async function issueGrant(env, licenseKey, machineId, plan) {
  const issuedAt = nowIso();
  const expiresAt = addDaysIso(TOKEN_TTL_DAYS);
  const sig = await hmacHex(env.SECRET, canonical(licenseKey, machineId, plan, issuedAt, expiresAt));
  return { ok: true, license_key: licenseKey, machine_id: machineId, plan, issued_at: issuedAt, expires_at: expiresAt, sig };
}

// 간단한 형식 확인만 한다 (정식 검증 아님) — 비어 있지 않고 @ 를 포함하는지만 본다.
function looksLikeEmail(s) { return typeof s === "string" && s.length <= 200 && /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(s); }

async function handleActivate(req, env) {
  const body = await readJson(req);
  const licenseKey = body?.license_key?.trim();
  const machineId = body?.machine_id?.trim();
  if (!licenseKey || !machineId) return fail("bad_request");
  // 연락처 정보는 클라이언트가 동의 체크박스를 켠 경우에만 보낸다. 동의(consent)가 true 가 아니면
  // 이메일이 같이 왔어도 통째로 무시해서, 서버가 동의 없는 개인정보를 저장할 길을 코드로 막아 둔다.
  const consentGiven = body?.consent === true && looksLikeEmail(body?.contact_email);
  const contactEmail = consentGiven ? body.contact_email.trim() : null;

  const neg = env.KV ? await env.KV.get(`neg:${licenseKey}`) : null;
  if (neg) return fail(neg, 403);

  const lic = await loadLicense(env.DB, licenseKey);
  if (!lic) { if (env.KV) await env.KV.put(`neg:${licenseKey}`, "not_found", { expirationTtl: NEG_CACHE_TTL_SEC }); return fail("not_found", 404); }
  if (lic.revoked) { if (env.KV) await env.KV.put(`neg:${licenseKey}`, "revoked", { expirationTtl: NEG_CACHE_TTL_SEC }); return fail("revoked", 403); }
  if (lic.expires_at && lic.expires_at < nowIso()) { if (env.KV) await env.KV.put(`neg:${licenseKey}`, "expired", { expirationTtl: NEG_CACHE_TTL_SEC }); return fail("expired", 403); }

  const existing = await env.DB.prepare("SELECT * FROM activations WHERE license_key = ? AND machine_id = ?")
    .bind(licenseKey, machineId).first();
  if (existing) {
    if (consentGiven) {
      await env.DB.prepare("UPDATE activations SET last_seen = ?, contact_email = ?, consent_at = ? WHERE license_key = ? AND machine_id = ?")
        .bind(nowIso(), contactEmail, nowIso(), licenseKey, machineId).run();
    } else {
      await env.DB.prepare("UPDATE activations SET last_seen = ? WHERE license_key = ? AND machine_id = ?")
        .bind(nowIso(), licenseKey, machineId).run();
    }
  } else {
    const countRow = await env.DB.prepare("SELECT COUNT(*) AS n FROM activations WHERE license_key = ?").bind(licenseKey).first();
    if ((countRow?.n ?? 0) >= lic.max_activations) return fail("machine_limit_reached", 403);
    await env.DB.prepare(
      "INSERT INTO activations (license_key, machine_id, activated_at, last_seen, contact_email, consent_at) VALUES (?, ?, ?, ?, ?, ?)"
    ).bind(licenseKey, machineId, nowIso(), nowIso(), contactEmail, consentGiven ? nowIso() : null).run();
  }
  return json(await issueGrant(env, licenseKey, machineId, lic.plan));
}

// 사용자가 동의를 철회한다: 그 기기의 연락처 이메일/동의 시각만 지운다 (활성화 자체는 유지 — 라이선스는 계속 쓸 수 있음).
// 관리자 키 없이도, license_key+machine_id 를 아는 그 기기 본인이 호출할 수 있다 (/validate 와 같은 인증 모델).
async function handleWithdrawContact(req, env) {
  const body = await readJson(req);
  const licenseKey = body?.license_key?.trim();
  const machineId = body?.machine_id?.trim();
  if (!licenseKey || !machineId) return fail("bad_request");
  await env.DB.prepare("UPDATE activations SET contact_email = NULL, consent_at = NULL WHERE license_key = ? AND machine_id = ?")
    .bind(licenseKey, machineId).run();
  return json({ ok: true });
}

async function handleValidate(req, env) {
  const body = await readJson(req);
  const licenseKey = body?.license_key?.trim();
  const machineId = body?.machine_id?.trim();
  if (!licenseKey || !machineId) return fail("bad_request");

  const lic = await loadLicense(env.DB, licenseKey);
  if (!lic) return fail("not_found", 404);
  if (lic.revoked) return fail("revoked", 403);
  if (lic.expires_at && lic.expires_at < nowIso()) return fail("expired", 403);

  const existing = await env.DB.prepare("SELECT * FROM activations WHERE license_key = ? AND machine_id = ?")
    .bind(licenseKey, machineId).first();
  if (!existing) return fail("not_activated", 403); // activate 를 먼저 거쳐야 한다

  await env.DB.prepare("UPDATE activations SET last_seen = ? WHERE license_key = ? AND machine_id = ?")
    .bind(nowIso(), licenseKey, machineId).run();
  return json(await issueGrant(env, licenseKey, machineId, lic.plan));
}

async function handleDeactivate(req, env) {
  const body = await readJson(req);
  const licenseKey = body?.license_key?.trim();
  const machineId = body?.machine_id?.trim();
  if (!licenseKey || !machineId) return fail("bad_request");
  await env.DB.prepare("DELETE FROM activations WHERE license_key = ? AND machine_id = ?").bind(licenseKey, machineId).run();
  return json({ ok: true });
}

function checkAdmin(req, env) {
  return env.ADMIN_KEY && req.headers.get("x-admin-key") === env.ADMIN_KEY;
}

async function handleAdminCreate(req, env) {
  if (!checkAdmin(req, env)) return fail("unauthorized", 401);
  const body = (await readJson(req)) || {};
  const plan = body.plan || "standard";
  const maxActivations = Number.isFinite(body.max_activations) ? body.max_activations : 1;
  const expiresAt = body.expires_at || null; // null = 영구
  let key;
  for (let i = 0; i < 5; i++) { // 충돌 거의 없지만, 혹시 겹치면 다시 뽑는다
    key = randomKey();
    const dup = await env.DB.prepare("SELECT 1 FROM licenses WHERE license_key = ?").bind(key).first();
    if (!dup) break;
  }
  await env.DB.prepare(
    "INSERT INTO licenses (license_key, email, plan, max_activations, revoked, expires_at, created_at) VALUES (?, ?, ?, ?, 0, ?, ?)"
  ).bind(key, body.email || null, plan, maxActivations, expiresAt, nowIso()).run();
  return json({ ok: true, license_key: key, plan, max_activations: maxActivations, expires_at: expiresAt });
}

async function handleAdminRevoke(req, env, revoked) {
  if (!checkAdmin(req, env)) return fail("unauthorized", 401);
  const body = (await readJson(req)) || {};
  if (!body.license_key) return fail("bad_request");
  await env.DB.prepare("UPDATE licenses SET revoked = ? WHERE license_key = ?").bind(revoked ? 1 : 0, body.license_key).run();
  if (env.KV) await env.KV.delete(`neg:${body.license_key}`);
  return json({ ok: true });
}

async function handleAdminLookup(req, env) {
  if (!checkAdmin(req, env)) return fail("unauthorized", 401);
  const key = new URL(req.url).searchParams.get("key");
  if (!key) return fail("bad_request");
  const lic = await loadLicense(env.DB, key);
  if (!lic) return fail("not_found", 404);
  const acts = await env.DB.prepare(
    "SELECT machine_id, activated_at, last_seen, contact_email, consent_at FROM activations WHERE license_key = ?"
  ).bind(key).all();
  return json({ ok: true, license: lic, activations: acts.results || [] });
}

export default {
  async fetch(req, env) {
    const url = new URL(req.url);
    // 브라우저가 커스텀 헤더(x-admin-key)를 보내기 전에 먼저 물어보는 사전 확인 요청(preflight)에 응답.
    if (req.method === "OPTIONS") return new Response(null, { status: 204, headers: CORS_HEADERS });
    if (req.method === "POST" && url.pathname === "/activate") return handleActivate(req, env);
    if (req.method === "POST" && url.pathname === "/validate") return handleValidate(req, env);
    if (req.method === "POST" && url.pathname === "/deactivate") return handleDeactivate(req, env);
    if (req.method === "POST" && url.pathname === "/withdraw-contact") return handleWithdrawContact(req, env);
    if (req.method === "POST" && url.pathname === "/admin/create") return handleAdminCreate(req, env);
    if (req.method === "POST" && url.pathname === "/admin/revoke") return handleAdminRevoke(req, env, true);
    if (req.method === "POST" && url.pathname === "/admin/unrevoke") return handleAdminRevoke(req, env, false);
    if (req.method === "GET" && url.pathname === "/admin/license") return handleAdminLookup(req, env);
    if (url.pathname === "/") return json({ ok: true, service: "ony-license" });
    return fail("not_found", 404);
  },
};
