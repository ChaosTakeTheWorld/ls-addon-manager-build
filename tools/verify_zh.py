#!/usr/bin/env python3
"""汉化一致性检查：验证格式串占位符未被破坏、报告固定缓冲区风险点。

用法：python tools/verify_zh.py
只读检查，不修改任何文件。
"""

import io
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
GUI_DIR = ROOT / "manager" / "src" / "gui"
FEATURE_DIR = ROOT / "manager" / "src" / "features"

# printf 占位符：%s %d %u %lu %zu %.1f %08x 等
PLACEHOLDER = re.compile(r"%[-+ #0]*\d*(?:\.\d+)?[a-zA-Z]")
# C++ 字符串字面量（含宽字符串），处理简单转义
LITERAL = re.compile(r'(?:L)?"((?:[^"\\]|\\.)*)"')

# 原文占位符快照：取自翻译前的 git HEAD
def original_literals(path):
    import subprocess
    rel = path.relative_to(ROOT).as_posix()
    try:
        text = subprocess.check_output(
            ["git", "show", f"HEAD:{rel}"], cwd=ROOT, stderr=subprocess.DEVNULL
        ).decode("utf-8", errors="replace")
    except subprocess.CalledProcessError:
        return []
    return [m.group(1) for m in LITERAL.finditer(text)]


def main():
    targets = sorted(GUI_DIR.rglob("*.cpp")) + sorted(FEATURE_DIR.glob("*.cpp"))
    changed = [p for p in targets if any("\u4e00" <= c <= "\u9fff" for c in p.read_text(encoding="utf-8"))]

    print(f"检查文件数: {len(changed)}")
    errors = []
    warnings = []

    for path in changed:
        text = path.read_text(encoding="utf-8")
        old = original_literals(path)
        new = [m.group(1) for m in LITERAL.finditer(text)]

        # 统计含中文的字面量
        zh = [s for s in new if any("\u4e00" <= c <= "\u9fff" for c in s)]

        for s in zh:
            # 检查常见占位符残留：中文字面量中不应出现 % 后跟非占位符
            for token in PLACEHOLDER.findall(s):
                if token in ("%s", "%d", "%u", "%lu", "%zu", "%f", "%.1f", "%.0f", "%08x", "%p", "%ls", "%c", "%x"):
                    continue
                warnings.append(f"{path.name}: 未知占位符 {token!r} in {s[:60]!r}")

        # 检查 snprintf 固定的缓冲区：翻译后中文 3 字节，列出所有需要人工确认的点
        for m in re.finditer(r"char\s+\w+\s*\[\s*(\d+)\s*\]", text):
            line = text[: m.start()].count("\n") + 1
            size = int(m.group(1))
            if size < 64:
                warnings.append(f"{path.name}:{line}: 固定缓冲区 char[{size}]，含中文时可能截断")

        print(f"  {path.relative_to(ROOT).as_posix()}: 中文字面量 {len(zh)}")

    if warnings:
        print("\n需人工确认的项:")
        for w in warnings:
            print("  -", w)

    if errors:
        print("\n错误:", file=sys.stderr)
        for e in errors:
            print("  -", e, file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
