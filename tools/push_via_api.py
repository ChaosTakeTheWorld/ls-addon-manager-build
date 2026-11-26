#!/usr/bin/env python3
"""通过 GitHub API 串行提交本地全部跟踪文件，绕过代理对 git CONNECT 的拦截。

流程：读取远程 HEAD -> 串行创建缺失的 blob -> 创建 tree -> 创建 commit -> 更新 ref。
对 403/429 二级限流做指数退避；进度写入 .push_progress.json 以支持断点续传。
"""

import base64
import hashlib
import json
import pathlib
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
REPO = "ChaosTakeTheWorld/ls-addon-manager-zh"
API = "https://api.github.com"
PROGRESS = ROOT / ".push_progress.json"


def get_token():
    out = subprocess.check_output(
        ["git", "credential", "fill"],
        input=b"protocol=https\nhost=github.com\n\n",
        cwd=ROOT,
        timeout=15,
    ).decode("utf-8", errors="replace")
    for line in out.splitlines():
        if line.startswith("password="):
            return line[len("password="):]
    raise RuntimeError("No token from git credential")


def api(path, token, method="GET", payload=None, timeout=90):
    url = f"{API}{path}"
    data = json.dumps(payload).encode("utf-8") if payload is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Authorization", f"Bearer {token}")
    req.add_header("Accept", "application/vnd.github+json")
    req.add_header("Content-Type", "application/json")
    req.add_header("User-Agent", "ls-addon-manager-zh-build")

    delay = 2.0
    for attempt in range(8):
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                return json.loads(resp.read().decode("utf-8"))
        except urllib.error.HTTPError as e:
            body = e.read().decode("utf-8", errors="replace")[:300]
            if e.code in (403, 429) and attempt < 7:
                retry = e.headers.get("Retry-After")
                wait = float(retry) if retry else delay
                print(f"  限流 {e.code}，等待 {wait:.0f}s ... ({body[:80]})", flush=True)
                time.sleep(wait)
                delay = min(delay * 2, 60)
                continue
            raise RuntimeError(f"HTTP {e.code} {path}: {body}")
        except Exception:
            if attempt == 7:
                raise
            time.sleep(delay)
            delay = min(delay * 2, 60)


def git_blob_sha(content):
    return hashlib.sha1(f"blob {len(content)}\0".encode("utf-8") + content).hexdigest()


def main():
    token = get_token()

    files = [f for f in subprocess.check_output(
        ["git", "ls-files", "-z"], cwd=ROOT
    ).decode("utf-8").split("\0") if f]
    files = [f for f in files if (ROOT / f).is_file()]

    done = set()
    if PROGRESS.exists():
        done = set(json.loads(PROGRESS.read_text(encoding="utf-8")).get("done", []))
    print(f"待处理文件: {len(files)}，已完成 blob: {len(done)}", flush=True)

    blob_sha = {}
    for rel in files:
        content = (ROOT / rel).read_bytes()
        sha = git_blob_sha(content)
        if rel in done:
            blob_sha[rel] = sha
            continue
        for attempt in range(4):
            try:
                blob = api(
                    f"/repos/{REPO}/git/blobs", token, method="POST",
                    payload={
                        "content": base64.b64encode(content).decode("ascii"),
                        "encoding": "base64",
                    },
                )
                blob_sha[rel] = blob["sha"]
                done.add(rel)
                break
            except RuntimeError as e:
                if attempt == 3:
                    raise
                print(f"  重试 {rel}: {str(e)[:100]}", flush=True)
                time.sleep(5 * (attempt + 1))
        if len(done) % 25 == 0:
            PROGRESS.write_text(json.dumps({"done": sorted(done)}), encoding="utf-8")
            print(f"  进度 {len(done)}/{len(files)}", flush=True)
        time.sleep(0.15)

    PROGRESS.write_text(json.dumps({"done": sorted(done)}), encoding="utf-8")
    print(f"全部 blob 就绪: {len(blob_sha)}", flush=True)

    # 读取远程 HEAD
    ref = api(f"/repos/{REPO}/git/ref/heads/main", token)
    parent = ref["object"]["sha"]
    print(f"远程 HEAD: {parent}", flush=True)

    # 完整 tree：保留远程已有条目（排除本次覆盖的），追加全部本地文件
    commit_obj = api(f"/repos/{REPO}/git/commits/{parent}", token)
    base_tree = api(f"/repos/{REPO}/git/trees/{commit_obj['tree']['sha']}", token)
    entries = []
    for e in base_tree.get("tree", []):
        if e["path"] in blob_sha:
            continue
        entries.append({"path": e["path"], "mode": e["mode"], "type": e["type"], "sha": e["sha"]})
    for rel, sha in blob_sha.items():
        entries.append({"path": rel, "mode": "100644", "type": "blob", "sha": sha})

    print(f"创建 tree: {len(entries)} 项", flush=True)
    tree = api(
        f"/repos/{REPO}/git/trees", token, method="POST",
        payload={"tree": entries},
    )

    commit = api(
        f"/repos/{REPO}/git/commits", token, method="POST",
        payload={
            "message": "汉化管理器界面：简体中文文本与中文字形支持",
            "tree": tree["sha"],
            "parents": [parent],
        },
    )
    api(
        f"/repos/{REPO}/git/refs/heads/main", token, method="PATCH",
        payload={"sha": commit["sha"], "force": False},
    )
    print(f"远程 HEAD: {commit['sha']}", flush=True)
    print("提交成功", flush=True)


if __name__ == "__main__":
    sys.exit(main())
