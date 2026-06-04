# Plan: enforce mcumgr UART buffer relations at build time

**Branch:** `mcumgr/uart-mtu-netbuf-build-assert`
**Scope:** `subsys/mgmt/mcumgr/transport/` (UART SMP transport)
**Status:** draft for review (intercreate/zephyr fork)

## Motivation

The mcumgr SMP UART transport has several buffers whose relationships are documented
in Kconfig prose as "must satisfy …" but are **not enforced at compile time**. A
misconfiguration is only discovered at runtime (silently dropped fragments / frames),
and the docs conflate the *encoded* (base64, on-wire) and *decoded* (reassembled)
sizes. This came out of an investigation into what an SMP client may send; see
[mcu-tools/mcuboot#2746](https://github.com/mcu-tools/mcuboot/pull/2746) and
[intercreate/smpclient#81](https://github.com/intercreate/smpclient/pull/81).

## How the UART transport actually buffers (as of this base)

A serial SMP frame is `[ uint16 length | SMP message | uint16 CRC16 ]`, base64-encoded
and split into `\n`-delimited lines. Three distinct buffers are involved:

| Role | Symbol (default) | Enc/Dec |
| --- | --- | --- |
| Per-fragment (one line) | `CONFIG_UART_MCUMGR_RX_BUF_SIZE` (128) | encoded |
| Fragment pool (queue depth) | `CONFIG_UART_MCUMGR_RX_BUF_COUNT` (2) | — |
| Reassembled frame | `CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE` (384) | decoded |
| Transport "MTU" | `CONFIG_MCUMGR_TRANSPORT_UART_MTU` (256) | — |

Key behaviors confirmed in source (base `ic/main`):

- `mcumgr_serial_decode_frag()` base64-decodes **each fragment as it arrives** straight
  into the netbuf at the running offset, bounded by `net_buf_tailroom()`
  (`subsys/mgmt/mcumgr/transport/src/serial_util.c`). The transport never holds the
  whole *encoded* frame at once; the **decoded** netbuf (`NETBUF_SIZE`) is the real
  per-transaction ceiling, and `os_mgmt` reports it as `buf_size`.
- A fragment longer than `UART_MCUMGR_RX_BUF_SIZE` is dropped ("Line too long")
  (`drivers/console/uart_mcumgr.c`); buffers are recycled as fragments are processed,
  so `RX_BUF_COUNT` is a throughput queue depth, not a frame-size cap.
- `smp_uart_get_mtu()` returns `UART_MTU`, but `get_mtu` is **not invoked on the UART
  path** in the core (only `smp_bt.c` calls its own `get_mtu`). So on UART, `UART_MTU`
  is effectively advisory; `NETBUF_SIZE` bounds the incoming request.

## Documented relations (today: prose only)

1. `MCUMGR_TRANSPORT_UART_MTU <= MCUMGR_TRANSPORT_NETBUF_SIZE + 2`
   — `subsys/mgmt/mcumgr/transport/Kconfig.uart`
2. `UART_MCUMGR_RX_BUF_COUNT * UART_MCUMGR_RX_BUF_SIZE >= MCUMGR_TRANSPORT_UART_MTU`
   (SMP-over-console only) — `drivers/console/Kconfig`

Only `BUILD_ASSERT(UART_MTU != 0)` exists in `smp_uart.c` today.

## Change in this PR

Add one `BUILD_ASSERT` to `smp_uart.c` enforcing **relation 1**:

```c
BUILD_ASSERT(CONFIG_MCUMGR_TRANSPORT_UART_MTU <= CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE + 2,
	     "CONFIG_MCUMGR_TRANSPORT_UART_MTU must be <= CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE + 2");
```

Why this is safe:

- `smp_uart.c` `depends on UART_MCUMGR && !UART_MCUMGR_RAW_PROTOCOL`, so all referenced
  symbols are always defined here and it is always the SMP-over-console transport.
- Both symbols' defaults satisfy it (256 ≤ 386), so no default/in-tree config breaks.
- It enforces the project's own documented "must satisfy" relation; a frame larger than
  the netbuf cannot be received regardless, so a larger `UART_MTU` is never valid.

## Deliberately *not* changed (open questions for maintainers)

- **Relation 2 is not asserted.** Because fragments are decoded-and-recycled
  incrementally, `RX_BUF_COUNT * RX_BUF_SIZE >= UART_MTU` appears to be a *throughput*
  heuristic rather than a hard requirement (a config with a smaller pool can still
  reassemble a larger frame, just with less headroom). Asserting it could reject
  configs that work today. Recommend the maintainers decide whether to (a) assert it,
  (b) relax the Kconfig wording, or (c) leave as guidance.
- **`UART_MTU` / `get_mtu` on UART.** The help text reads "size of SMP frames sent and
  received," but `get_mtu` is never called on the UART path and `NETBUF_SIZE` is the
  real incoming bound. A help-text clarification (and/or wiring `get_mtu` to response
  framing) may be warranted, but is a behavior/semantics question left out of this PR.

## Validation

- Builds with defaults unchanged (relation holds with margin).
- Empirically (smpclient integration suite against native_sim/QEMU/mps2 Zephyr SMP
  servers): a `NETBUF_SIZE − 4` message round-trips while the on-wire encoded frame is
  ~1.37× `NETBUF_SIZE` (e.g. `NETBUF_SIZE=2048` → 2044-byte message → 2801-byte,
  23-line frame), confirming the decoded netbuf — not any encoded buffer — is the
  ceiling.

## References

- `serial_util.c` decode-into-netbuf: https://github.com/zephyrproject-rtos/zephyr/blob/a65b87f2c6d5961b105193bd94058e523d6df54f/subsys/mgmt/mcumgr/transport/src/serial_util.c#L48-L64
- `uart_mcumgr.c` per-fragment cap: https://github.com/zephyrproject-rtos/zephyr/blob/a65b87f2c6d5961b105193bd94058e523d6df54f/drivers/console/uart_mcumgr.c#L110-L118
- mcuboot serial recovery (mirror architecture): https://github.com/mcu-tools/mcuboot/pull/2746
