#!/usr/bin/env python3
"""云端构建驱动：写入 workflow -> 触发构建 -> 轮询 -> 下载 Lossless.dll。

前置条件：GitHub token 需包含 workflow scope。
用法：python tools/cloud_build.py
"""

import base64
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
WORKFLOW = "build-zh.yml"


def get_token():
    # 优先使用环境变量 GITHUB_TOKEN；否则退回 git 凭据。
    import os
    env = os.environ.get("GITHUB_TOKEN", "").strip()
    if env:
        return env
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


def api(path, token, method="GET", payload=None, timeout=60):
    url = f"{API}{path}"
    data = json.dumps(payload).encode("utf-8") if payload is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Authorization", f"Bearer {token}")
    req.add_header("Accept", "application/vnd.github+json")
    req.add_header("Content-Type", "application/json")
    req.add_header("User-Agent", "ls-addon-manager-zh-cloud-build")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        body = resp.read().decode("utf-8")
        return json.loads(body) if body else {}


def main():
    token = get_token()

    # 1. 写入 workflow 文件（已存在时需带 sha）
    wf = (ROOT / ".github" / "workflows" / WORKFLOW).read_bytes()
    payload = {"message": "添加汉化构建 workflow", "content": base64.b64encode(wf).decode("ascii")}
    wf_path = f"/repos/{REPO}/contents/.github/workflows/{WORKFLOW}"
    print("写入 workflow ...", flush=True)
    try:
        existing = api(f"{wf_path}?ref=main", token)
        if "sha" in existing:
            payload["sha"] = existing["sha"]
    except Exception:
        pass
    api(wf_path, token, method="PUT", payload=payload)

    # 2. 触发 workflow_dispatch
    print("触发构建 ...", flush=True)
    api(
        f"/repos/{REPO}/actions/workflows/{WORKFLOW}/dispatches",
        token, method="POST", payload={"ref": "main"},
    )

    # 3. 轮询最近的 workflow run
    run_id = None
    for i in range(30):
        time.sleep(3)
        runs = api(f"/repos/{REPO}/actions/workflows/{WORKFLOW}/runs?per_page=1", token)
        items = runs.get("workflow_runs", [])
        if not items:
            continue
        run = items[0]
        run_id = run["id"]
        status = run["status"]
        print(f"  run {run_id}: {status} / {run.get('conclusion')}", flush=True)
        if status == "completed":
            if run.get("conclusion") != "success":
                raise RuntimeError(f"构建失败: {run.get('conclusion')}")
            break
    else:
        raise RuntimeError("构建超时")

    # 4. 下载 artifact
    print("下载 artifact ...", flush=True)
    artifacts = api(f"/repos/{REPO}/actions/runs/{run_id}/artifacts", token)
    urls = artifacts.get("artifacts", [])
    if not urls:
        raise RuntimeError("未找到构建产物")
    artifact = urls[0]
    print(f"  artifact: {artifact['name']} ({artifact['size_in_bytes']} bytes)", flush=True)

    req = urllib.request.Request(artifact["archive_download_url"])
    req.add_header("Authorization", f"Bearer {token}")
    req.add_header("User-Agent", "ls-addon-manager-zh-cloud-build")
    out_dir = ROOT / "dist"
    out_dir.mkdir(exist_ok=True)
    zip_path = out_dir / "Lossless-zh.zip"
    with urllib.request.urlopen(req, timeout=120) as resp, open(zip_path, "wb") as f:
        f.write(resp.read())
    print(f"已下载: {zip_path}", flush=True)

    # 5. 解压并校验
    import zipfile
    with zipfile.ZipFile(zip_path) as z:
        names = z.namelist()
        print(f"  内容: {names}", flush=True)
        for name in names:
            if name.endswith("Lossless.dll"):
                dll = z.read(name)
                dll_path = out_dir / "Lossless.dll"
                dll_path.write_bytes(dll)
                print(f"DLL: {dll_path} ({len(dll)} bytes)", flush=True)
                return 0
    raise RuntimeError("artifact 中未找到 Lossless.dll")


if __name__ == "__main__":
    sys.exit(main())
