#!/usr/bin/env python3
"""kit login | logout | status | mcp, and kit submit's upload: the kit and the developer platform (developer.tikpal.ai).

  developer.py login [--no-open] [--url <site>]
  developer.py logout [--url <site>]
  developer.py status <app> [--lang zh|en] [--for-ai] [--url <site>]
  developer.py mcp [claude|cursor|json] [--url <site>]

login signs the kit in as the developer the way a TV does: a code, the developer allows it on the site, the kit gets
a token (ykd_...). It is kept in $HOME/.yaykit-devkit/developer.json (or $YAYKIT_DEVKIT_HOME), only this user may read
it, in the same file and form as `kit miniapp login`'s, so either sign-in serves both. The token is never printed.
status says where the app is in plain words and, returned or held, every flagged problem by file and line. mcp prints
how to connect the developer's own AI to the platform's MCP, with the token left to an environment variable.
kit submit (submit_app.py) uploads its package and submits it with upload_and_submit.

The site is https://developer.tikpal.ai, or --url, or TIKPAL_DEVELOPER_URL: https, or plain http on this computer only.
No request follows a redirect (BOSS-2, #1465): urllib's would carry Authorization to wherever it points, another host
or plain http alike; a 3xx is an error here. The package goes only to an https address (this computer's for testing).
Standard library only.
"""
import datetime
import hashlib
import json
import os
import socket
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import webbrowser

DEFAULT_URL = "https://developer.tikpal.ai"
LOCAL = ("localhost", "127.0.0.1", "[::1]", "::1")
MCP_NAME = "tikpal-developer"
TOKEN_ENV = "TIKPAL_DEVELOPER_TOKEN"
NOTES_MAX = 500
LEVEL = {"block": "要改", "suggest": "建议"}
BY = {"machine": "机器", "ai": "AI", "device": "真机", "person": "人"}

ERROR_WORDS = {
    "OFFLINE": "连不上开发者网站：看看网络",
    "CLI_TOKEN_INVALID": "登录过期了，或者在网站上退出了：./kit login 重新登录",
    "UNAUTHENTICATED": "登录过期了，或者在网站上退出了：./kit login 重新登录",
    "TOKEN_SCOPE": "这台电脑的登录做不了这件事：多半是旧的登录（只能传小程序开发版），./kit login 重新登录",
    "TOKEN_APP": "这台电脑的登录碰不了这个应用：./kit login 重新登录",
    "WEB_ONLY": "这件事只能在开发者网站上登录了做",
    "APP_NOT_FOUND": "开发者网站上没有这个应用，或者不是你的：核对 app.json 的 slug",
    "NOT_DEVELOPER": "这个账号还不是开发者：先在开发者网站用邀请码申请",
    "TERMS_OUTDATED": "开发者协议更新了：先到开发者网站读完签了",
    "DEVELOPER_UNAVAILABLE": "开发者平台暂时连不上，过一会儿再试",
    "RATE_LIMIT": "调得太频繁了：过一分钟再试（上传每小时最多 10 次）",
    "SUBMISSION_OPEN": "这个应用已经有一份没办完的提交：等它办完，或者先撤回（开发者网站上，或者 MCP 的 withdraw_submission）再交",
    "STORE_INCOMPLETE": "商店素材不全，要你自己补（平台不代做）：到开发者网站这个应用的「商店素材」补齐",
    "PACKAGE_INVALID": "这个包交不了",
    "PACKAGE_TOO_LARGE": "包超过 30 MB",
    "UPLOAD_MISSING": "包没传到，或者大小、SHA-256 对不上：再 ./kit submit 一次",
    "UPLOAD_EXPIRED": "上传地址过期了：再 ./kit submit 一次",
    "UPLOAD_FAILED": "包没传上去（存储没收下）：再 ./kit submit 一次",
    "INSECURE_URL": "登录只发给 https 的开发者网站（本机测试除外）：核对 --url、TIKPAL_DEVELOPER_URL",
    "REDIRECTED": "开发者网站让开发包转去别的地址，开发包不跟（免得把登录带去别处）：核对 --url、TIKPAL_DEVELOPER_URL；还这样就到开发者网站上交",
    "APP_UNLISTED": "这个应用下架了：要重新上架，找 Tikpal",
    "NOT_AVAILABLE": "开发者平台还没有这个接口：先到网站这个应用的「版本」页上传，或者过些天再试",
    "CLI_PENDING": "网页上还没点「允许」",
    "CLI_DENIED": "网页上没有允许，没登录",
    "CLI_EXPIRED": "码过期了，再来一次：./kit login",
}


class PlatformError(Exception):
    """The platform said no (or could not be reached): its code, status, what it said and its details."""

    def __init__(self, code, status=0, said=None, details=None, problems=None):
        super().__init__(code)
        self.code, self.status, self.said, self.details, self.problems = code, status, said, details or {}, problems or []

    def words(self):
        text = ERROR_WORDS.get(self.code) or self.said or f"开发者平台回了 {self.code}"
        if self.code == "TOKEN_SCOPE" and self.details.get("scope"):
            text += f"（缺 {self.details['scope']}）"
        if self.code == "STORE_INCOMPLETE" and self.details.get("missing"):
            text += "（缺：" + "、".join(str(m) for m in self.details["missing"][:20]) + "）"
        if self.code == "SUBMISSION_OPEN" and self.details.get("id"):
            text += f"（提交 {self.details['id']}，{self.details.get('state', '?')}）"
        return text


class NoRedirect(urllib.request.HTTPRedirectHandler):
    """A 3xx is answered as it is (an HTTPError), never followed: following it would send Authorization along."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


OPENER = urllib.request.build_opener(NoRedirect)


def secure(url):
    """Whether an address may be sent a token or a package: https, or plain http on this computer only."""
    try:
        parts = urllib.parse.urlsplit(url)
        host = parts.hostname or ""
    except ValueError:
        return False
    return bool(host) and (parts.scheme == "https" or (parts.scheme == "http" and host in LOCAL))


def site_url(raw):
    """The site's origin: https, or plain http on this computer only (a token never goes out in the clear)."""
    try:
        url = urllib.parse.urlsplit(raw)
    except ValueError:
        raise ValueError(f"开发者网站的地址不对：{raw}")
    if not secure(raw):
        raise ValueError(f"开发者网站要用 https（本机测试除外）：{raw}")
    return f"{url.scheme}://{url.netloc}"


def default_site(given=None):
    return site_url(given or os.environ.get("TIKPAL_DEVELOPER_URL") or DEFAULT_URL)


def token_file():
    home = os.environ.get("YAYKIT_DEVKIT_HOME") or os.path.join(os.path.expanduser("~"), ".yaykit-devkit")
    return os.path.join(home, "developer.json")


def read_signed():
    try:
        with open(token_file(), encoding="utf-8") as f:
            kept = json.load(f)
        return kept if isinstance(kept, dict) and kept.get("token") else None
    except (OSError, ValueError):
        return None


def expired(kept, now=None):
    try:
        at = datetime.datetime.fromisoformat(str(kept["expiresAt"]).replace("Z", "+00:00"))
    except (KeyError, ValueError):
        return True
    return at <= (now or datetime.datetime.now(datetime.timezone.utc))


def signed_in(site):
    """The sign-in kept for `site`, if it has not run out; None otherwise."""
    kept = read_signed()
    if not kept or kept.get("url") != site or expired(kept):
        return None
    return kept


def keep(signed):
    """Written whole or not at all: a file of its own (0600) first, then put in place."""
    path = token_file()
    os.makedirs(os.path.dirname(path), mode=0o700, exist_ok=True)
    nxt = f"{path}.{os.getpid()}.tmp"
    fd = os.open(nxt, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        f.write(json.dumps(signed, ensure_ascii=False, indent=2) + "\n")
    os.chmod(nxt, 0o600)
    os.replace(nxt, path)


def lang_of(given=None):
    """zh or en: --lang, else the shell's locale (zh_CN… is Chinese), else Chinese."""
    if given in ("zh", "en"):
        return given
    loc = os.environ.get("LC_ALL") or os.environ.get("LC_MESSAGES") or os.environ.get("LANG") or ""
    return "en" if loc and not loc.lower().startswith("zh") and loc not in ("C", "POSIX") and not loc.startswith("C.") else "zh"


def call(url, method="GET", body=None, token=None, data=None, headers=None, lang=None, timeout=60):
    """One request, never following a redirect; the platform's JSON answer, or PlatformError with its code."""
    if token and not secure(url):
        raise PlatformError("INSECURE_URL")
    sent = dict(headers or {})
    if token:
        sent["Authorization"] = f"Bearer {token}"
    if lang:
        sent["Accept-Language"] = "zh-CN" if lang == "zh" else "en"
    if body is not None:
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")
        sent["Content-Type"] = "application/json"
    request = urllib.request.Request(url, data=data, method=method, headers=sent)
    try:
        with OPENER.open(request, timeout=timeout) as response:
            text = response.read()
            status = response.status
    except urllib.error.HTTPError as e:
        text, status = e.read(), e.code
    except (urllib.error.URLError, OSError):
        raise PlatformError("OFFLINE")
    try:
        answer = json.loads(text) if text else {}
    except ValueError:
        answer = None
    if 200 <= status < 300:
        if answer is None:
            raise PlatformError(f"HTTP_{status}", status)
        return answer
    if 300 <= status < 400:
        raise PlatformError("REDIRECTED", status)
    error = (answer or {}).get("error") if isinstance(answer, dict) else None
    error = error if isinstance(error, dict) else {}
    code = error.get("code") or ("NOT_AVAILABLE" if status == 404 else "RATE_LIMIT" if status == 429 else f"HTTP_{status}")
    said = error.get("message") if isinstance(error.get("message"), str) else None
    details = error.get("details") if isinstance(error.get("details"), dict) else {}
    problems = error.get("problems") if isinstance(error.get("problems"), list) else []
    raise PlatformError(code, status, said and said[:500], details, problems)


# ---------------------------------------------------------------- signing in


def login(site, open_browser=True, say=print, sleep=time.sleep):
    """A code, shown with the page to allow it on, then asking every `interval` seconds until it is allowed."""
    name = (socket.gethostname() or "computer")[:60]
    asked = call(f"{site}/v1/developer/cli/start", "POST", {"name": name})
    page = f"{site}/cli?code={asked['userCode']}"
    say(f"在浏览器里打开 {page}")
    say(f"用你的开发者账号登录，核对码是 {asked['userCode']}，点「允许」（{round(asked['expiresIn'] / 60)} 分钟内有效）")
    if open_browser:
        try:
            webbrowser.open(page)
        except Exception:  # the address is printed anyway
            pass
    before = read_signed()
    until = time.monotonic() + asked["expiresIn"]
    while time.monotonic() < until:
        sleep(asked["interval"])
        try:
            issued = call(f"{site}/v1/developer/cli/token", "POST", {"deviceCode": asked["deviceCode"]})
        except PlatformError as e:
            # not allowed yet, asked too soon, or the network or the platform in trouble: wait and ask again
            if e.code in ("CLI_PENDING", "RATE_LIMIT", "OFFLINE") or e.status >= 500:
                continue
            if e.code in ("CLI_DENIED", "CLI_EXPIRED"):
                raise SystemExit(f"kit: {ERROR_WORDS[e.code]}")
            raise
        signed = {"url": site, "token": issued["token"], "expiresAt": issued["expiresAt"], "name": issued["developer"]["name"]}
        keep(signed)
        # the sign-in this one replaces is signed out on the platform too, so no token is left behind working
        if before and before.get("url") == site and before.get("token") != signed["token"] and not expired(before):
            try:
                call(f"{site}/v1/developer/cli/me", "DELETE", token=before["token"])
            except PlatformError:
                pass
        say(f"登录好了：{signed['name']}（{signed['expiresAt'][:10]} 到期）")
        return signed
    raise SystemExit(f"kit: {ERROR_WORDS['CLI_EXPIRED']}")


def logout(site):
    """The platform forgets the token, and so does this computer. One kept for another site is left as it is."""
    kept = read_signed()
    if not kept:
        return None
    if kept.get("url") != site:
        return {"other": kept.get("url")}
    if not expired(kept):
        try:
            call(f"{site}/v1/developer/cli/me", "DELETE", token=kept["token"])
        except PlatformError:
            pass
    try:
        os.remove(token_file())
    except OSError:
        pass
    return {"name": kept.get("name", "")}


# ---------------------------------------------------------------- submitting


def upload_and_submit(signed, slug, package, notes, lang=None):
    """kit submit: an address for the package (POST /v1/developer/uploads), the package PUT there as it says, then
    submitted with what this version does (POST /v1/developer/apps/<slug>/submissions). The submission, queued."""
    site, token = signed["url"], signed["token"]
    sha = hashlib.sha256(package).hexdigest()
    upload = call(f"{site}/v1/developer/uploads", "POST", {"slug": slug, "size": len(package), "sha256": sha}, token=token)
    if not secure(str(upload.get("url", ""))):
        raise PlatformError("UPLOAD_FAILED", 0, "the upload address is not https")
    headers = dict(upload.get("headers") or {})
    if not any(k.lower() == "content-type" for k in headers):
        headers["Content-Type"] = "application/octet-stream"
    headers = {k: v for k, v in headers.items() if k.lower() not in ("content-length", "host")}
    # the store's address, signed: never the token
    try:
        call(upload["url"], upload.get("method", "PUT"), data=package, headers=headers, timeout=600)
    except PlatformError as e:
        raise PlatformError("OFFLINE" if e.code == "OFFLINE" else "UPLOAD_FAILED", e.status)
    path = f"/v1/developer/apps/{urllib.parse.quote(slug, safe='')}/submissions"
    return call(f"{site}{path}", "POST", {"uploadId": upload["uploadId"], "notes": notes}, token=token, lang=lang_of(lang))


# ---------------------------------------------------------------- where it is


def status_of(signed, slug, lang):
    """GET …/status, and the findings when it was returned or held."""
    site, token = signed["url"], signed["token"]
    q = urllib.parse.quote(slug, safe="")
    status = call(f"{site}/v1/developer/apps/{q}/status?lang={lang}", token=token)
    findings = None
    sub = status.get("submission") or {}
    if status.get("status") in ("returned", "held") and sub.get("id"):
        findings = call(f"{site}/v1/developer/submissions/{urllib.parse.quote(sub['id'], safe='')}/findings?lang={lang}", token=token)
    return status, findings


def status_lines(status, findings):
    """In plain words: where it is, what is next, the latest submission, every flagged problem by file and line."""
    out = [f"{status['slug']}：{status['now']}", f"下一步：{status['next']}"]
    sub = status.get("submission")
    if sub:
        parts = "，".join(p for p in [f"固件 {sub['firmware']}" if sub.get("firmware") else "", f"小程序 {sub['miniapp']}" if sub.get("miniapp") else ""] if p)
        out.append(f"提交 {sub['id']}（{parts or sub.get('state', '')}）")
    rel = status.get("released") or {}
    if rel.get("preview") or rel.get("stable"):
        out.append("发出去的：" + "，".join(f"{k} {v}" for k, v in [("预览版", rel.get("preview")), ("正式版", rel.get("stable"))] if v))
    items = (findings or {}).get("findings") or []
    if items:
        out.append("")
        out.append(f"拦下的问题（{len(items)} 条，以这里为准，别猜）：")
        by_file = {}
        for f in items:
            by_file.setdefault(f.get("file") or "（商店素材和别的，没有文件）", []).append(f)
        for file, group in by_file.items():
            out.append(file)
            for f in sorted(group, key=lambda x: (x.get("line") or 0)):
                where = f"{f['line']} 行  " if f.get("line") else ""
                out.append(f"  {where}[{LEVEL.get(f.get('level'), f.get('level'))}] {f.get('rule')}（{BY.get(f.get('by'), f.get('by'))}）{f.get('why', '')}")
                if f.get("quote"):
                    out.append(f"      > {f['quote']}")
                if f.get("fix"):
                    out.append(f"      怎么改：{f['fix']}")
    return out


# ---------------------------------------------------------------- the MCP


def mcp_config(site, which=None):
    """How to connect the developer's AI to the platform's MCP, the token left to $TIKPAL_DEVELOPER_TOKEN."""
    url = f"{site}/mcp"
    claude = f'claude mcp add --transport http {MCP_NAME} {url} --header "Authorization: Bearer ${TOKEN_ENV}"'
    cursor = {"mcpServers": {MCP_NAME: {"url": url, "headers": {"Authorization": f"Bearer ${{env:{TOKEN_ENV}}}"}}}}
    other = {"mcpServers": {MCP_NAME: {"type": "http", "url": url, "headers": {"Authorization": f"Bearer ${{{TOKEN_ENV}}}"}}}}
    parts = {
        "claude": ["Claude Code（一行命令，在你的应用目录里跑）：", claude],
        "cursor": ["Cursor（~/.cursor/mcp.json 或者项目的 .cursor/mcp.json）：", json.dumps(cursor, indent=2)],
        "json": ["别的 MCP 客户端（比如项目的 .mcp.json）：", json.dumps(other, indent=2)],
    }
    head = [
        f"Tikpal 开发者平台的 MCP：{url}（无状态 HTTP，令牌放在 Authorization 里）",
        f"1. 在开发者网站（{site}）上建一个令牌：起个名字，选能碰哪些应用、做哪些事（建应用、改商店素材要它全有）。令牌只显示一次。",
        f"2. 存进环境变量 {TOKEN_ENV}（比如写进 shell 的配置），别写进仓库、别贴到公开的地方。",
        "3. 照你用的 AI 接上：",
        "",
    ]
    body = []
    for key in ([which] if which else ["claude", "cursor", "json"]):
        body += parts[key] + [""]
    tail = ["接上以后，让 AI 先调 whoami 看令牌能做什么；被退回时调 get_findings 看拦下的问题，以它为准。"]
    return "\n".join(head + body + tail)


# ---------------------------------------------------------------- the commands


def read_slug(app):
    try:
        with open(os.path.join(app, "app.json"), encoding="utf-8") as f:
            slug = json.load(f).get("slug")
    except (OSError, ValueError) as e:
        raise SystemExit(f"kit: app.json 读不了：{e}")
    if not slug:
        raise SystemExit("kit: app.json 没有 slug")
    return slug


def main(argv):
    if len(argv) < 2:
        print(__doc__.strip().splitlines()[2].strip())
        return 2
    command, rest = argv[1], argv[2:]
    opts, positional, i = {}, [], 0
    while i < len(rest):
        a = rest[i]
        if a in ("--url", "--lang") and i + 1 < len(rest):
            opts[a[2:]] = rest[i + 1]
            i += 2
            continue
        if a in ("--no-open", "--for-ai"):
            opts[a[2:]] = True
        elif a.startswith("--"):
            print(f"kit {command}：不认识 {a}")
            return 2
        else:
            positional.append(a)
        i += 1
    try:
        site = default_site(opts.get("url"))
    except ValueError as e:
        print(f"kit: {e}")
        return 2
    try:
        if command == "login":
            login(site, open_browser=not opts.get("no-open"))
        elif command == "logout":
            was = logout(site)
            if not was:
                print("本来就没登录")
            elif "other" in was:
                print(f"这台电脑登录的是 {was['other']}，没动它；要退出那个：./kit logout --url {was['other']}")
            else:
                print(f"退出了（{was['name']}）")
        elif command == "status":
            if not positional:
                print("kit status：在应用目录里跑")
                return 2
            signed = signed_in(site)
            if not signed:
                print("没登录：./kit login（或者到开发者网站看这个应用的那一页）")
                return 1
            lang = lang_of(opts.get("lang"))
            status, findings = status_of(signed, read_slug(positional[0]), lang)
            print(status["forAI"] if opts.get("for-ai") else "\n".join(status_lines(status, findings)))
        elif command == "mcp":
            which = positional[0] if positional else None
            if which not in (None, "claude", "cursor", "json"):
                print("kit mcp：claude、cursor 或 json")
                return 2
            print(mcp_config(site, which))
        else:
            print(f"kit：不认识 {command}")
            return 2
    except PlatformError as e:
        print(f"kit: {e.words()}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
