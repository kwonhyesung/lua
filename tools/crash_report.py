"""크래시 하나를 한 번에 진단: 최신 crashpad .dmp 파싱(예외코드/주소/모듈) +
out/hook.log 마지막 부분 + Player.log 마지막 부분을 모아 리포트 파일로 저장한다.

사용법:
  python tools/crash_report.py                 최신 dmp 자동 탐색
  python tools/crash_report.py <dmp경로>        특정 dmp 지정

출력: 콘솔에 요약 + out/crash_report_<타임스탬프>.txt 에 전체 내용 저장(UTF-8, 콘솔 인코딩 문제 없음).
"""
import glob
import os
import struct
import sys
import time

HOME = os.path.expanduser("~")
REPORTS_DIR = os.path.join(HOME, "AppData", "LocalLow", "Nexon", "MapleStory Worlds", "backtrace", "crashpad", "reports")
PLAYER_LOG = os.path.join(HOME, "AppData", "LocalLow", "Nexon", "MapleStory Worlds", "Player.log")
REPO_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOOK_LOG = os.path.join(REPO_DIR, "out", "hook.log")
OUT_DIR = os.path.join(REPO_DIR, "out")


def find_latest_dump():
    dumps = glob.glob(os.path.join(REPORTS_DIR, "*.dmp"))
    if not dumps:
        return None
    return max(dumps, key=os.path.getmtime)


def parse_minidump(path):
    """MINIDUMP_HEADER를 파싱해 모듈 목록 + 예외 정보를 뽑는다. 실패해도 예외를 던지지 않고 dict로 원인을 담아 반환."""
    out = {"file": path, "mtime": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(os.path.getmtime(path)))}
    try:
        with open(path, "rb") as f:
            data = f.read()
        magic, version, num_streams, stream_dir_rva = struct.unpack_from("<IIII", data, 0)
        if magic != 0x504D444D:
            out["error"] = "not a minidump (bad magic)"
            return out
        streams = {}
        for i in range(num_streams):
            off = stream_dir_rva + i * 12
            stype, size, rva = struct.unpack_from("<III", data, off)
            streams[stype] = (rva, size)

        modules = []
        if 4 in streams:  # ModuleListStream
            rva, _ = streams[4]
            count = struct.unpack_from("<I", data, rva)[0]
            for i in range(count):
                moff = rva + 4 + i * 108  # sizeof(MINIDUMP_MODULE)
                base, msize = struct.unpack_from("<QI", data, moff)
                name_rva = struct.unpack_from("<I", data, moff + 20)[0]  # ModuleNameRva (BaseOfImage:8+SizeOfImage:4+CheckSum:4+TimeDateStamp:4=20)
                nlen = struct.unpack_from("<I", data, name_rva)[0]
                if nlen > 1024 or name_rva + 4 + nlen > len(data):
                    name = "(unparseable module name)"
                else:
                    name = data[name_rva + 4:name_rva + 4 + nlen].decode("utf-16-le", errors="replace")
                modules.append((base, base + msize, name))
        out["modules"] = modules

        if 6 in streams:  # ExceptionStream
            rva, _ = streams[6]
            thread_id, _align = struct.unpack_from("<II", data, rva)
            code, flags, record, addr = struct.unpack_from("<IIQQ", data, rva + 8)
            out["thread_id"] = thread_id
            out["exception_code"] = code
            out["faulting_addr"] = addr
            for base, end, name in modules:
                if base <= addr < end:
                    out["module"] = name
                    out["offset"] = addr - base
                    break
        else:
            out["error"] = "no exception stream (not a crash dump, or truncated)"
    except Exception as e:
        out["error"] = f"parse failed: {e}"
    return out


def tail_file(path, n=60):
    if not os.path.exists(path):
        return f"(not found: {path})"
    try:
        with open(path, "rb") as f:
            f.seek(0, os.SEEK_END)
            size = f.tell()
            block = 8192
            data = b""
            while f.tell() > 0 and data.count(b"\n") <= n:
                step = min(block, f.tell())
                f.seek(-step, os.SEEK_CUR)
                data = f.read(step) + data
                f.seek(-step, os.SEEK_CUR)
        text = data.decode("utf-8", errors="replace")
        return "\n".join(text.splitlines()[-n:])
    except Exception as e:
        return f"(error reading {path}: {e})"


def rotated_hook_logs():
    """out/ 폴더의 hook_*.log 중 최신 것도 후보로 쓴다 (마지막 hook.log가 방금 덮어써졌을 수 있어서)."""
    pattern = os.path.join(OUT_DIR, "hook_*.log")
    files = glob.glob(pattern)
    if not files:
        return None
    return max(files, key=os.path.getmtime)


def build_report(dump_path):
    lines = []
    lines.append("=" * 70)
    lines.append(f"crash_report.py — {time.strftime('%Y-%m-%d %H:%M:%S')}")
    lines.append("=" * 70)

    lines.append("\n-- crash dump --")
    if dump_path:
        info = parse_minidump(dump_path)
        lines.append(f"file: {info['file']}")
        lines.append(f"mtime: {info['mtime']}")
        if "exception_code" in info:
            lines.append(f"thread={info['thread_id']} exception_code=0x{info['exception_code']:08X} "
                          f"faulting_addr=0x{info['faulting_addr']:016X}")
            if "module" in info:
                lines.append(f"-> in module {info['module']} at offset 0x{info['offset']:X}")
            else:
                lines.append("-> address not in any known module (native/JIT/unknown)")
            if info["exception_code"] == 0x80000004:
                lines.append("NOTE: STATUS_SINGLE_STEP — our own HWBP (luaL_loadbufferx hook) most likely, "
                              "check faulting_addr against hook.log's 'ARM ... dr0=' value.")
        if "error" in info:
            lines.append(f"error: {info['error']}")
    else:
        lines.append("(no .dmp found — if the process just vanished with no dump, it's likely "
                      "TerminateProcess from outside, not a native fault: check hook.log for whether "
                      "DllMain's '=== process exit ===' line is present at all)")

    lines.append("\n-- out/hook.log (tail) --")
    lines.append(tail_file(HOOK_LOG, 60))

    latest_rotated = rotated_hook_logs()
    if latest_rotated and os.path.abspath(latest_rotated) != os.path.abspath(HOOK_LOG):
        lines.append(f"\n-- latest rotated log: {os.path.basename(latest_rotated)} (tail) --")
        lines.append(tail_file(latest_rotated, 30))

    lines.append("\n-- Player.log (tail) --")
    lines.append(tail_file(PLAYER_LOG, 60))

    return "\n".join(lines)


def main():
    dump_path = sys.argv[1] if len(sys.argv) > 1 else find_latest_dump()
    report = build_report(dump_path)

    os.makedirs(OUT_DIR, exist_ok=True)
    out_path = os.path.join(OUT_DIR, f"crash_report_{time.strftime('%Y%m%d_%H%M%S')}.txt")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(report)

    try:
        print(report)
    except UnicodeEncodeError:
        print(report.encode("ascii", errors="backslashreplace").decode("ascii"))
    print(f"\n(saved to {out_path})")


if __name__ == "__main__":
    main()
