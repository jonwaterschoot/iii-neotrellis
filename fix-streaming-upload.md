# Fix: stream script upload directly to flash

**Branch:** `fix/streaming-upload`  
**Base:** iii v1.1.2 (`ed920ef`)  
**Files changed:** `repl.c`, `vm.c`, `vm.h`

---

## Problem

Uploading Lua scripts larger than ~32 KB via diii causes a device hard fault and leaves it unreachable (USB serial port inaccessible, power cycle required).

Two compounding bugs in `repl.c`:

**Bug 1 — Memory leak on buffer-full:**  
`REPL_CMD_START_DATA` mallocs 32 KB for `script_buf`. When the buffer fills mid-upload, `reset_script_rx()` clears `in_rx_script` but does not free `script_buf`. The `^^W` handler's `free()` is inside the `if(in_rx_script)` branch — now unreachable — so 32 KB leaks permanently.

**Bug 2 — Post-overflow execution triggers OOM → hard fault:**  
After `in_rx_script` is cleared, remaining incoming lines are routed to `vm_run_buffer()`. The first Lua heap allocation fails due to the leaked 32 KB. `my_alloc` calls `watchdog_reboot(0, 0, 1000)` but immediately returns `NULL` to the Lua VM, which dereferences it — hard fault before the watchdog fires. The `// TODO: actually do something else?` comment in `my_alloc` already flagged this path as unsafe.

**Effective threshold is lower than `SCRIPT_BUFFER_SIZE`:**  
diii transmits with CRLF (`\r\n`). Since `repl_handle_byte` triggers on both `\r` and `\n`, each line causes two writes — content and an empty line (+1 byte). Effective buffer usage is `file_size + line_count`. For typical Lua line lengths the practical ceiling is ~31–32 KB, not the theoretical 32,766 bytes.

---

## Fix

Replace the fixed-size heap buffer with a streaming approach:

- `REPL_CMD_START_DATA` opens `.uploading.tmp` on LittleFS instead of `malloc`
- Each incoming line is written directly to flash via `fs_file_write` — no RAM accumulation, no size limit
- `vm_test_file()` compiles from the temp file using a 256-byte chunked `lua_load()` reader (new in `vm.c`) — the Lua parser never needs the full script in RAM
- On success, `fs_rename(".uploading.tmp", filename)` atomically finalises the upload
- On failure or abort, the temp file is removed — no leak possible
- `vm_init` updated to stream `lib.lua` and `init.lua` the same way, removing the malloc+read pattern at boot too

---

## Full investigation

Complete bug analysis, reproduction steps, test scripts, and upstream issue draft:

[leaveseqr-upload-failure-investigation.md](https://github.com/jonwaterschoot/neotrellis-monome/blob/main/neotrellis_monome_picosdk_iii/doc/changestracker/leaveseqr-upload-failure-investigation.md#step-6--upstream-bug-report-draft--ready-to-file)

The investigation includes:
- Confirmed threshold: scripts ≥ 32,304 bytes crash the upstream v1.1.2 build
- Minimal reproducer test scripts bracketing the exact failure point
- Side-by-side comparison of upstream vs fix architecture
- Confirmed working on scripts up to 57 KB (with color API on neotrellis fork)

---

## What's new compared to PR #8 / issue #9

PR [#8](https://codeberg.org/tehn/iii/pulls/8) and issue [#9](https://codeberg.org/tehn/iii/issues/9) proposed the same streaming approach (from the `fix/25kb-streaming` branch). That PR was described as "failing as-is" — this branch explains why, and fixes it.

### The missing piece: v1.1.2 memory improvements

The streaming fix requires enough heap headroom for the Lua VM to operate while `lua_load()` compiles from flash. PR #8 predated v1.1.2. The three memory improvements that landed in v1.1.2 are what make the streaming approach viable:

1. **`LUA_32BITS`** — Lua integers and floats shrink from 8 bytes to 4 bytes, recovering ~8–9 KB of heap
2. **Library removals** — `io`, `os`, `utf8`, `package`, `coroutine` stripped from `luaopen_libs`; no longer loaded into the VM at boot
3. **`lua_gc(L, LUA_GCCOLLECT)` after each lib** — each standard library load is followed by a forced GC cycle, keeping the heap clean during init

This branch is rebased on v1.1.2 (`ed920ef`) and preserves all three improvements. Without them there is not enough free heap for `lua_load()` to compile even a mid-sized script — which is exactly why tehn's own `chunks` branch also crashed when tested against the pre-v1.1.2 codebase.

### Precise root cause now documented

PR #8 described the symptom (buffer full / crash). This branch adds the full two-bug chain:

- **Bug 1**: `reset_script_rx()` clears `in_rx_script` without freeing `script_buf` — 32 KB permanently leaked
- **Bug 2**: remaining incoming lines are routed to `vm_run_buffer()`; first Lua allocation fails; `my_alloc` calls `watchdog_reboot()` then returns `NULL`; Lua dereferences `NULL` → hard fault before watchdog fires → USB unreachable

The `// TODO: actually do something else?` comment in `my_alloc` already flagged this path as unsafe.

### Exact failure threshold measured

Using minimal reproducer scripts that bracket the exact overflow point (accounting for diii's CRLF transmission — effective buffer usage is `file_size + num_lines`):

| Script | Size | Lines | Effective | Result |
|--------|------|-------|-----------|--------|
| `threshold_test_under.lua` | 32,240 B | 505 | 32,745 B | ✅ passes |
| `threshold_test_over.lua`  | 32,304 B | 506 | 32,810 B | ❌ hard fault |

### Cleaner implementation scope

PR #8 also modified `lib_lua.c`. This branch touches only the three files that need changing: `repl.c`, `vm.c`, `vm.h`. No embedded resource changes.
