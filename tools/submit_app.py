#!/usr/bin/env python3
"""kit submit: packs one version of an app for review and, the kit signed in (kit login), uploads and submits it.

  submit_app.py <kit> <app> [--notes <text>] [--no-upload] [--miniapp-only] [--ci-sha <sha256>] [--image <file>]
                [--no-check] [--heap <file>] [--out <file>] [--url <site>] [--lang zh|en]

The package (<slug>-<version>.tikpal, a zip) holds the source as `git archive HEAD` gives it (the kit in devkit/ is
named by its tag and commit, not copied), app.json, the release image built here, the 小程序's zip, kit check's
output and what else the developer adds (report/heap.json from --heap, report/ai-selfreview.json, report/shots/*.png),
and submission.json listing each file with its SHA-256 (Tikpal's contract, packages/contracts/src/submissions.ts).
Review rebuilds the firmware from the commit; the image here is only compared with it.

It never changes the repository. Signed in, it then uploads the package and submits it to the developer platform with
--notes (what this version does, shown to users), and prints the submission's number; `kit status` follows it (both in
developer.py). --no-upload, or not signed in, only packs, as before: the package is uploaded on the site then.
--miniapp-only packs a version that changes only the 小程序 (no image, no kit check). --no-check packs without running kit check or anything of the app's (the review runs them).
--ci-sha is the SHA-256 the app repository's CI printed: given, the rebuild must match it byte for byte. --image packs
another build of this version (the CI's, downloaded) instead of build-release's. Standard library only.
"""
import datetime
import hashlib
import io
import json
import os
import re
import subprocess
import sys
import tarfile
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import developer  # noqa: E402

FORMAT = 1
PACKAGE_MAX = 30 * 1024 * 1024
FILES_MAX = 5000
SHOTS_MAX = 8
SHA256 = re.compile(r"^[0-9a-f]{64}$")
GITHUB = re.compile(r"^(?:https://github\.com/|git@github\.com:|ssh://git@github\.com/)([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+?)(?:\.git)?/?$")
SEMVER = re.compile(r"^(0|[1-9]\d{0,3})\.(0|[1-9]\d{0,4})\.(0|[1-9]\d{0,5})$")
# Zip entries get one fixed time, so the same files always make the same package.
FIXED_TIME = (1980, 1, 1, 0, 0, 0)


class Stop(Exception):
    pass


def git(cwd, *args):
    r = subprocess.run(["git", "-c", "safe.directory=*", "-C", cwd, *args], capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else None


def sha256_of(data):
    return hashlib.sha256(data).hexdigest()


def github_repository(app):
    """The app repository's address on GitHub, https://github.com/<owner>/<repo>, from a remote that has HEAD."""
    remotes = (git(app, "remote") or "").split()
    for name in ["origin"] + [r for r in remotes if r != "origin"]:
        if name not in remotes:
            continue
        m = GITHUB.match(git(app, "remote", "get-url", name) or "")
        if not m:
            continue
        pushed = git(app, "branch", "-r", "--contains", "HEAD") or ""
        if not any(line.strip().startswith(name + "/") for line in pushed.splitlines()):
            raise Stop(f"这个提交还没推到 {name}（GitHub）：审核按提交号从 GitHub 重编，先 git push 再 ./kit submit")
        return f"https://github.com/{m.group(1)}/{m.group(2)}"
    raise Stop("应用仓库没有 GitHub 上的远端：第一版审核要从 GitHub 按提交号重编，先建好仓库、推上去")


def read_json(path, what):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError) as e:
        raise Stop(f"{what} 读不了：{e}")


def miniapp_part(app, slug):
    """The 小程序's zip as `npm run build` left it (miniapp/<slug>-<version>.zip), and its version."""
    manifest = read_json(os.path.join(app, "miniapp", "manifest.json"), "miniapp/manifest.json")
    version = manifest.get("version", "")
    if manifest.get("slug") != slug or not SEMVER.match(str(version)):
        raise Stop(f"miniapp/manifest.json 的 slug 要是 {slug}、version 要是三段式版本号")
    path = os.path.join(app, "miniapp", f"{slug}-{version}.zip")
    if not os.path.exists(path):
        raise Stop(f"没找到 miniapp/{slug}-{version}.zip：在 miniapp/ 里 npm run build 打一个")
    data = open(path, "rb").read()
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            inside = json.loads(z.read("manifest.json"))
    except (KeyError, ValueError, zipfile.BadZipFile):
        raise Stop(f"miniapp/{slug}-{version}.zip 不是打包工具打的（里面没有 manifest.json）：重新 npm run build")
    if inside.get("version") != version or not inside.get("files"):
        raise Stop(f"miniapp/{slug}-{version}.zip 不是现在这个版本打的：重新 npm run build")
    return version, f"miniapp/{slug}-{version}.zip", data


def firmware_part(app, version, image_path):
    """The release image: build-release's (./kit build --release), or --image's."""
    if image_path:
        data = open(image_path, "rb").read()
        name = os.path.basename(image_path)
    else:
        desc = read_json(os.path.join(app, "build-release", "project_description.json"), "build-release（先 ./kit build --release）")
        if desc.get("project_version") != version:
            raise Stop(f"build-release 是 {desc.get('project_version')} 编的，不是 {version}：重新 ./kit build --release")
        name = desc["app_bin"]
        data = open(os.path.join(app, "build-release", name), "rb").read()
    if len(data) < 0x200 or data[0] != 0xE9:
        raise Stop(f"{name} 不是 ESP 应用镜像")
    return f"firmware/{name}", data


def source_tar(app):
    """git archive HEAD: committed files only, devkit/ left out (a submodule is not archived)."""
    r = subprocess.run(["git", "-c", "safe.directory=*", "-C", app, "archive", "--format=tar", "HEAD"], capture_output=True)
    if r.returncode:
        raise Stop("git archive 没成功：" + r.stderr.decode("utf-8", "replace").strip())
    return r.stdout


def run_check(kit, app):
    """kit check, as ./kit check runs it: its whole output goes into the report; it has to pass."""
    r = subprocess.run([sys.executable, os.path.join(kit, "tools", "check_app.py"), kit, app], capture_output=True, text=True)
    out = r.stdout + r.stderr
    if r.returncode:
        raise Stop("./kit check 没过，改完再 ./kit submit：\n" + out.rstrip())
    return out


def made_at():
    now = datetime.datetime.now().astimezone().replace(microsecond=0)
    return now.isoformat()


def pack(kit, app, miniapp_only=False, ci_sha=None, image=None, no_check=False, heap=None, platform=None):
    """(file name, package bytes, submission.json) for the app at `app`; Stop says why not."""
    kit_info = read_json(os.path.join(kit, "devkit.json"), "devkit.json")
    spec = read_json(os.path.join(app, "app.json"), "app.json")
    slug, version = spec.get("slug", ""), spec.get("version", "")
    if not slug or not SEMVER.match(str(version)):
        raise Stop("app.json 要有 slug 和三段式的 version")
    if ci_sha is not None and not SHA256.match(ci_sha):
        raise Stop("--ci-sha 是 64 位小写十六进制的 SHA-256（应用仓库 CI 里 ./kit check 打的那一串）")
    if miniapp_only and (ci_sha or image):
        raise Stop("--miniapp-only 不带镜像，不用 --ci-sha、--image")

    if git(app, "status", "--porcelain", "--untracked-files=no"):
        raise Stop("有没提交的改动：审核按提交号重编，没提交的不算。提交以后再 ./kit submit")
    commit = git(app, "rev-parse", "HEAD")
    tree = git(app, "rev-parse", "HEAD^{tree}")
    if not commit or not tree:
        raise Stop("这里不是一个有提交的 git 仓库")
    repository = github_repository(app)
    devkit = os.path.join(app, "devkit")
    tag = git(devkit, "describe", "--tags", "--exact-match") if os.path.isdir(devkit) else None
    kit_commit = git(devkit, "rev-parse", "HEAD") if tag else None
    if not tag or not re.match(r"^v\d{1,3}\.\d{1,3}$", tag) or git(devkit, "status", "--porcelain"):
        raise Stop("devkit/ 不在开发包的正式版本上，或者被改过：./kit update <版本> 换回去，提交以后再交")

    files = {"app.json": open(os.path.join(app, "app.json"), "rb").read(), "source.tar": source_tar(app)}
    manifest = {
        "format": FORMAT,
        "slug": slug,
        "parts": [],
        "repository": repository,
        "commit": commit,
        "tree": tree,
        "devkit": {"tag": tag, "commit": kit_commit},
    }
    if not miniapp_only:
        if not no_check:
            files["report/kit-check.txt"] = run_check(kit, app).encode("utf-8")
        path, data = firmware_part(app, version, image)
        digest = sha256_of(data)
        on_linux = (platform or sys.platform).startswith("linux")
        built = "linux" if (ci_sha == digest or (on_linux and not image)) else "mac"
        if built == "linux" and ci_sha and ci_sha != digest:
            raise Stop("这台 Linux 上编的镜像跟 --ci-sha 不一样：多半是 CI 编的不是这个提交，或者本机有没提交的东西")
        files[path] = data
        manifest["parts"].append("firmware")
        manifest["firmware"] = {"version": version, "file": path, "size": len(data), "sha256": digest, "built": built, "ciSha256": ci_sha}
    has_miniapp = "miniapp" in spec or os.path.exists(os.path.join(app, "miniapp", "manifest.json"))
    if miniapp_only and not has_miniapp:
        raise Stop("--miniapp-only，但这个应用没有小程序（app.json 没有 miniapp 一段）")
    if has_miniapp:
        if "miniapp" not in spec:
            raise Stop("有 miniapp/，app.json 却没有 miniapp 一段：照 kit miniapp new 打出来的那段补上")
        m_version, path, data = miniapp_part(app, slug)
        files[path] = data
        manifest["parts"].append("miniapp")
        manifest["miniapp"] = {"version": m_version, "file": path, "size": len(data), "sha256": sha256_of(data)}

    if heap:
        files["report/heap.json"] = json.dumps(read_json(heap, "--heap 的文件"), ensure_ascii=False, indent=2).encode("utf-8")
    self_review = os.path.join(app, "ai-selfreview.json")
    if os.path.exists(self_review):
        files["report/ai-selfreview.json"] = json.dumps(read_json(self_review, "ai-selfreview.json"), ensure_ascii=False, indent=2).encode("utf-8")
    shots = os.path.join(app, "shots")
    if os.path.isdir(shots):
        pngs = sorted(n for n in os.listdir(shots) if n.lower().endswith(".png"))
        if len(pngs) > SHOTS_MAX:
            raise Stop(f"shots/ 里有 {len(pngs)} 张截图，最多 {SHOTS_MAX} 张")
        for n in pngs:
            if not re.match(r"^[A-Za-z0-9._-]{1,80}$", n):
                raise Stop(f"截图的文件名 {n} 只用字母、数字和 . _ -")
            files[f"report/shots/{n}"] = open(os.path.join(shots, n), "rb").read()

    manifest["files"] = {name: sha256_of(data) for name, data in sorted(files.items())}
    manifest["kit"] = kit_info["version"]
    manifest["madeAt"] = made_at()
    if len(files) + 1 > FILES_MAX:
        raise Stop(f"包里文件太多：{len(files) + 1} 个，最多 {FILES_MAX} 个")

    out = io.BytesIO()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        body = json.dumps(manifest, ensure_ascii=False, indent=2).encode("utf-8") + b"\n"
        for name, data in [("submission.json", body)] + sorted(files.items()):
            info = zipfile.ZipInfo(name, FIXED_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)
    package = out.getvalue()
    if len(package) > PACKAGE_MAX:
        raise Stop(f"提交包 {len(package)} 字节，超过 {PACKAGE_MAX}（30 MB）：大文件（录音、视频）别放进仓库")
    name_version = version if not miniapp_only else manifest["miniapp"]["version"]
    return f"{slug}-{name_version}.tikpal", package, manifest


def tar_files(data):
    """Paths in a tar, for the summary."""
    with tarfile.open(fileobj=io.BytesIO(data)) as t:
        return [m.name for m in t.getmembers() if m.isfile()]


def main(argv):
    if len(argv) < 3:
        print(__doc__.strip().splitlines()[2].strip())
        return 2
    kit, app, rest = argv[1], argv[2], argv[3:]
    opts = {"miniapp_only": False, "ci_sha": None, "image": None, "no_check": False, "heap": None}
    out = notes = site = lang = None
    upload = True
    i = 0
    while i < len(rest):
        a = rest[i]
        if a in ("--miniapp-only", "--no-check"):
            opts[a[2:].replace("-", "_")] = True
        elif a == "--no-upload":
            upload = False
        elif a in ("--ci-sha", "--image", "--out", "--heap", "--notes", "--url", "--lang") and i + 1 < len(rest):
            i += 1
            if a == "--out":
                out = rest[i]
            elif a == "--notes":
                notes = rest[i].strip()
            elif a == "--url":
                site = rest[i]
            elif a == "--lang":
                lang = rest[i]
            else:
                opts[a[2:].replace("-", "_")] = rest[i]
        else:
            print(f"kit submit：不认识 {a}")
            return 2
        i += 1
    # Signed in, it goes on to submit: what this version does is asked for before anything is packed.
    signed = None
    if upload:
        try:
            signed = developer.signed_in(developer.default_site(site))
        except ValueError as e:
            print(f"✗ {e}")
            return 2
    if signed and not notes:
        print(f"✗ 交审要写这一版做了什么（给用户看，{developer.NOTES_MAX} 字以内）：./kit submit --notes '……'。只打包不交：./kit submit --no-upload")
        return 2
    if notes and len(notes) > developer.NOTES_MAX:
        print(f"✗ --notes 超过 {developer.NOTES_MAX} 字")
        return 2
    try:
        name, package, manifest = pack(kit, app, **opts)
    except Stop as e:
        print(f"✗ {e}")
        return 1
    path = out or os.path.join(app, name)
    with open(path, "wb") as f:
        f.write(package)
    print(f"· 提交号 {manifest['commit']}，开发包 {manifest['devkit']['tag']}，{manifest['repository']}")
    if "firmware" in manifest:
        fw = manifest["firmware"]
        where = "CI 编的，审核重编要一字不差" if fw["built"] == "linux" and fw["ciSha256"] else ("这台机器编的" if fw["built"] == "mac" else "Linux 上编的")
        print(f"· 固件 {fw['version']}：{os.path.basename(fw['file'])}，SHA-256 {fw['sha256']}（{where}）")
    if "miniapp" in manifest:
        print(f"· 小程序 {manifest['miniapp']['version']}：{manifest['miniapp']['size']} 字节")
    print(f"· 源码 {len(tar_files(package_file(package, 'source.tar')))} 个文件")
    if not signed:
        print(f"✓ 打好了：{path}（{len(package)} 字节）。")
        if upload:
            print("  没登录，没上传：./kit login 以后再 ./kit submit --notes '这一版做了什么' 就直接交审；也可以到 developer.tikpal.ai 这个应用的「版本」页上传。")
        else:
            print("  没上传（--no-upload）：到 developer.tikpal.ai 这个应用的「版本」页上传，或者用 MCP 的 create_upload、submit_version 交。")
        return 0
    print(f"· 打好了：{path}（{len(package)} 字节），上传、交审……")
    try:
        sub = developer.upload_and_submit(signed, manifest["slug"], package, notes, lang)
    except developer.PlatformError as e:
        print(f"✗ {e.words()}。包在 {path}。")
        for p in e.problems[:50]:
            if isinstance(p, dict):
                where = (p.get("file") or "") + (f":{p['line']}" if p.get("line") else "")
                print(f"  ✗ {where or '包'}  {p.get('code')}  {json.dumps(p.get('params') or {}, ensure_ascii=False)}")
        return 1
    print(f"✓ 交上去了：提交 {sub.get('id')}（{sub.get('state')}）。./kit status 看进度；被退回时它按文件和行列出拦下的问题。")
    return 0


def package_file(package, name):
    with zipfile.ZipFile(io.BytesIO(package)) as z:
        return z.read(name)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
