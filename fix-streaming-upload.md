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

[leaveseqr-upload-failure-investigation.md](https://github.com/jonwaterschoot/neotrellis-monome/blob/dev/colors-v2/neotrellis_monome_picosdk_iii/doc/changestracker/leaveseqr-upload-failure-investigation.md#step-6--upstream-bug-report-draft--ready-to-file)

The investigation includes:
- Confirmed threshold: scripts ≥ 32,304 bytes crash the upstream v1.1.2 build
- Minimal reproducer test scripts bracketing the exact failure point
- Side-by-side comparison of upstream vs fix architecture
- Confirmed working on scripts up to 57 KB (with color API on neotrellis fork)
