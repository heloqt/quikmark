#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exe="${LITEMARK_LINUX_EXE:-${project_root}/build/linux-release/bin/quikmark}"
wx_lib_dir="${LITEMARK_WX_LIB_DIR:-/opt/wxWidgets-3.3.0/lib}"
cold_samples="${1:-5}"
hot_samples="${2:-5}"
switch_samples="${3:-10}"
large_samples="${4:-3}"

for command_name in Xvfb xdotool python3 timeout; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "Required command not found: ${command_name}" >&2
        exit 1
    fi
done
if [[ ! -x "${exe}" ]]; then
    echo "Linux executable not found: ${exe}" >&2
    exit 1
fi

output_dir="${project_root}/build/linux-xvfb-performance"
fixture_dir="${output_dir}/fixtures"
config_root="${output_dir}/config"
trace_dir="${output_dir}/traces"
rm -rf "${output_dir}"
mkdir -p "${fixture_dir}" "${config_root}" "${trace_dir}"

python3 - "${fixture_dir}" "${cold_samples}" "${hot_samples}" "${large_samples}" <<'PY'
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
regular_count = max(int(sys.argv[2]), int(sys.argv[3]))
large_count = int(sys.argv[4])

def write_fixture(path, target_size, marker):
    body = (
        "This paragraph exercises Markdown parsing, wrapping and drawing. "
        "中文段落用于验证 UTF-8 文本换行和字体回退。\n\n"
        "- item one\n- item two\n- item three\n\n"
        "```text\ncode sample\n```\n\n"
    ).encode("utf-8")
    size = 0
    with path.open("wb") as output:
        index = 0
        while size < target_size:
            header = f"## {marker} section {index}\n\n".encode("utf-8")
            output.write(header)
            output.write(body)
            size += len(header) + len(body)
            index += 1

for index in range(regular_count):
    write_fixture(
        root / f"regular-{index:02d}.md",
        153_600,
        f"regular-{index}",
    )
for index in range(large_count):
    write_fixture(
        root / f"large-{index:02d}.md",
        10_813_440,
        f"large-{index}",
    )
PY

display_number=121
while [[ -e "/tmp/.X${display_number}-lock" ]]; do
    display_number=$((display_number + 1))
done
display=":${display_number}"
Xvfb "${display}" -screen 0 1280x800x24 >"${output_dir}/xvfb.log" 2>&1 &
xvfb_pid=$!
primary_pid=""
cleanup() {
    if [[ -n "${primary_pid}" ]] && kill -0 "${primary_pid}" 2>/dev/null; then
        kill -TERM "${primary_pid}" 2>/dev/null || true
        wait "${primary_pid}" 2>/dev/null || true
    fi
    if kill -0 "${xvfb_pid}" 2>/dev/null; then
        kill -TERM "${xvfb_pid}" 2>/dev/null || true
        wait "${xvfb_pid}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

for _ in $(seq 1 100); do
    if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
        break
    fi
    sleep 0.05
done
export DISPLAY="${display}"
if [[ -d "${wx_lib_dir}" ]]; then
    export LD_LIBRARY_PATH="${wx_lib_dir}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
fi

metric_count() {
    local trace="$1"
    local event="$2"
    [[ -f "${trace}" ]] || {
        echo 0
        return
    }
    awk -F, -v event="${event}" '$1 == event { count++ } END { print count + 0 }' \
        "${trace}"
}

wait_metric() {
    local trace="$1"
    local event="$2"
    local count="$3"
    local timeout_seconds="${4:-20}"
    local attempts=$((timeout_seconds * 20))
    for ((attempt = 0; attempt < attempts; ++attempt)); do
        if [[ "$(metric_count "${trace}" "${event}")" -ge "${count}" ]]; then
            return 0
        fi
        sleep 0.05
    done
    echo "Timed out waiting for ${event} #${count} in ${trace}" >&2
    return 1
}

cold_trace="${trace_dir}/cold-150kb.csv"
for ((index = 0; index < cold_samples; ++index)); do
    config="${config_root}/cold-${index}"
    fixture="${fixture_dir}/regular-$(printf '%02d' "${index}").md"
    timeout 20 env \
        LITEMARK_CONFIG_DIR="${config}" \
        LITEMARK_PERF_TRACE="${cold_trace}" \
        "${exe}" --smoke-test "${fixture}"
    wait_metric "${cold_trace}" cold_start $((index + 1))
done

hot_trace="${trace_dir}/interactive-150kb.csv"
interactive_config="${config_root}/interactive"
env \
    LITEMARK_CONFIG_DIR="${interactive_config}" \
    LITEMARK_PERF_TRACE="${hot_trace}" \
    "${exe}" "${fixture_dir}/regular-00.md" \
    >"${output_dir}/primary.log" 2>&1 &
primary_pid=$!
wait_metric "${hot_trace}" hot_open 1 30

for ((index = 1; index < hot_samples; ++index)); do
    fixture="${fixture_dir}/regular-$(printf '%02d' "${index}").md"
    env \
        LITEMARK_CONFIG_DIR="${interactive_config}" \
        LITEMARK_PERF_TRACE="${hot_trace}" \
        "${exe}" "${fixture}" \
        >"${output_dir}/secondary-${index}.log" 2>&1
    wait_metric "${hot_trace}" hot_open $((index + 1)) 20
done

window_id=""
for _ in $(seq 1 100); do
    window_id="$(
        xdotool search --onlyvisible --pid "${primary_pid}" 2>/dev/null |
            head -n 1 || true
    )"
    [[ -n "${window_id}" ]] && break
    sleep 0.05
done
if [[ -z "${window_id}" ]]; then
    echo "Unable to locate the QuikMark X11 window." >&2
    exit 1
fi
xdotool windowfocus "${window_id}"
for ((index = 0; index < switch_samples; ++index)); do
    xdotool key --clearmodifiers ctrl+Tab
    wait_metric "${hot_trace}" tab_switch $((index + 1)) 8
done

kill -TERM "${primary_pid}" 2>/dev/null || true
wait "${primary_pid}" 2>/dev/null || true
primary_pid=""

large_trace="${trace_dir}/cold-10mb.csv"
for ((index = 0; index < large_samples; ++index)); do
    config="${config_root}/large-${index}"
    fixture="${fixture_dir}/large-$(printf '%02d' "${index}").md"
    timeout 30 env \
        LITEMARK_CONFIG_DIR="${config}" \
        LITEMARK_PERF_TRACE="${large_trace}" \
        "${exe}" --smoke-test "${fixture}"
    wait_metric "${large_trace}" cold_start $((index + 1)) 30
done

python3 - "${trace_dir}" "${exe}" "${output_dir}" <<'PY'
import hashlib
import json
import math
import pathlib
import sys

trace_dir = pathlib.Path(sys.argv[1])
exe = pathlib.Path(sys.argv[2])
output_dir = pathlib.Path(sys.argv[3])

def values(path, event):
    result = []
    if not path.exists():
        return result
    for line in path.read_text(encoding="utf-8").splitlines():
        name, raw = line.split(",", 1)
        if name == event:
            result.append(float(raw) / 1000.0)
    return result

def stats(samples):
    if not samples:
        return {"Samples": 0}
    ordered = sorted(samples)
    def percentile(p):
        index = max(0, math.ceil(len(ordered) * p) - 1)
        return round(ordered[index], 3)
    return {
        "Samples": len(ordered),
        "Min": round(ordered[0], 3),
        "P50": percentile(0.50),
        "P95": percentile(0.95),
        "Max": round(ordered[-1], 3),
    }

cold = values(trace_dir / "cold-150kb.csv", "cold_start")
interactive = trace_dir / "interactive-150kb.csv"
large = values(trace_dir / "cold-10mb.csv", "cold_start")
result = {
    "Environment": {
        "Runner": "Xvfb",
        "DisplayScale": "Xvfb 1280x800x24",
        "Exe": str(exe),
        "ExeBytes": exe.stat().st_size,
        "ExeSha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
    },
    "ColdStart150KBMs": stats(cold),
    "HotOpen150KBMs": stats(values(interactive, "hot_open")),
    "TabSwitch150KBMs": stats(values(interactive, "tab_switch")),
    "ColdStart10MBMs": stats(large),
}
(output_dir / "summary.json").write_text(
    json.dumps(result, ensure_ascii=False, indent=2),
    encoding="utf-8",
)
print(json.dumps(result, ensure_ascii=False, indent=2))
PY

echo "Results: ${output_dir}/summary.json"
