/*
 * ymodem.c — a real YMODEM (CRC-16 variant) sender + receiver, parameterized
 * over a plain non-blocking read/write pair (orion_xfer_io_t) rather than
 * hardcoded to serial -- see ymodem.h's own header comment for why (the
 * user's explicit ask: "serial for now but later ble, wifi, and so on").
 *
 * Standard enough to interoperate with common PC-side tools without any
 * custom counterpart: `sz --ymodem <file>` (lrzsz) or a terminal's "send
 * file" feature pairs with orion_ymodem_receive() below; `rz --ymodem` or
 * "receive file" pairs with orion_ymodem_send().
 *
 * Pure C99, no platform headers -- orion_millis()/orion_delay_ms() are the
 * only non-standard calls, both already declared in orion.h and safe to
 * call from any task.
 */
#include "ymodem.h"
#include "orion.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#define SOH 0x01
#define STX 0x02
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18
#define CRC_CHAR 'C'

static uint16_t crc16_ccitt(const uint8_t* buf, size_t len)
{
    uint16_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)buf[i] << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

static int xfer_read_byte(const orion_xfer_io_t* io, uint8_t* out, uint32_t timeout_ms)
{
    uint32_t start = orion_millis();
    for (;;) {
        if (io->read(io->ctx, out, 1) == 1) return 1;
        if (orion_millis() - start >= timeout_ms) return 0;
        orion_delay_ms(5);
    }
}

static void xfer_write_bytes(const orion_xfer_io_t* io, const uint8_t* buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        size_t n = io->write(io->ctx, buf + off, len - off);
        off += n;
        if (n == 0) orion_delay_ms(2);
    }
}

/* ── send side ────────────────────────────────────────────────────────── */

static int send_block(const orion_xfer_io_t* io, uint8_t block_num,
                      const uint8_t* data, size_t data_len,
                      const orion_xfer_progress_t* prog)
{
    uint8_t pkt[3 + 1024 + 2];
    pkt[0] = (data_len == 1024) ? STX : SOH;
    pkt[1] = block_num;
    pkt[2] = (uint8_t)(255 - block_num);
    memcpy(pkt + 3, data, data_len);
    uint16_t crc = crc16_ccitt(data, data_len);
    pkt[3 + data_len]     = (uint8_t)(crc >> 8);
    pkt[3 + data_len + 1] = (uint8_t)(crc & 0xFF);
    size_t pkt_len = 3 + data_len + 2;

    for (int attempt = 0; attempt < 10; ++attempt) {
        if (prog && prog->should_cancel && prog->should_cancel(prog->ctx)) return 0;
        xfer_write_bytes(io, pkt, pkt_len);
        uint8_t rx;
        if (xfer_read_byte(io, &rx, 3000)) {
            if (rx == ACK) return 1;
            if (rx == CAN) return 0;
            /* NAK (or garbage) -> retry the same block */
        }
    }
    return 0;
}

static int wait_for_crc_char(const orion_xfer_io_t* io, uint32_t total_timeout_ms,
                             const orion_xfer_progress_t* prog)
{
    uint32_t start = orion_millis();
    for (;;) {
        if (prog && prog->should_cancel && prog->should_cancel(prog->ctx)) return 0;
        uint8_t rx;
        if (xfer_read_byte(io, &rx, 500) && rx == CRC_CHAR) return 1;
        if (orion_millis() - start >= total_timeout_ms) return 0;
    }
}

orion_xfer_result_t orion_ymodem_send(const orion_xfer_io_t* io, const char* filepath,
                                      const orion_xfer_progress_t* prog)
{
    FILE* fp = fopen(filepath, "rb");
    if (!fp) return ORION_XFER_ERR_IO;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return ORION_XFER_ERR_IO; }
    long fsize = ftell(fp);
    if (fsize < 0 || fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return ORION_XFER_ERR_IO; }

    const char* base = strrchr(filepath, '/');
    base = base ? base + 1 : filepath;

    if (!wait_for_crc_char(io, 60000, prog)) { fclose(fp); return ORION_XFER_ERR_TIMEOUT; }

    /* Block 0 (header): "name\0size\0", zero-padded to 128 bytes. */
    uint8_t hdr[128];
    memset(hdr, 0, sizeof hdr);
    /* Explicit precision: `base` is derived from the caller's `filepath`
     * parameter, unbounded from the compiler's view -- same
     * -Wformat-truncation pattern as `filename`/`dest_dir` elsewhere in
     * this file. 127 preserves the exact same truncation behavior as the
     * bare "%s" this replaces (full buffer available). */
    size_t hlen = (size_t)snprintf((char*)hdr, sizeof hdr, "%.127s", base) + 1;
    if (hlen < sizeof hdr) snprintf((char*)hdr + hlen, sizeof(hdr) - hlen, "%ld", fsize);
    if (!send_block(io, 0, hdr, sizeof hdr, prog)) { fclose(fp); return ORION_XFER_ERR_PROTOCOL; }

    if (!wait_for_crc_char(io, 10000, prog)) { fclose(fp); return ORION_XFER_ERR_TIMEOUT; }

    uint8_t block_num = 1;
    uint8_t buf[1024];
    size_t sent = 0;
    for (;;) {
        size_t n = fread(buf, 1, sizeof buf, fp);
        if (n == 0) break;
        if (n < sizeof buf) memset(buf + n, 0x1A, sizeof(buf) - n); /* CPMEOF pad */
        if (!send_block(io, block_num, buf, sizeof buf, prog)) { fclose(fp); return ORION_XFER_ERR_PROTOCOL; }
        block_num++;
        sent += n;
        if (prog && prog->on_progress) prog->on_progress(prog->ctx, base, sent, (size_t)fsize);
        if (prog && prog->should_cancel && prog->should_cancel(prog->ctx)) {
            uint8_t can[2] = { CAN, CAN };
            xfer_write_bytes(io, can, 2);
            fclose(fp);
            return ORION_XFER_ERR_CANCELLED;
        }
    }
    fclose(fp);

    uint8_t eot = EOT, rx;
    xfer_write_bytes(io, &eot, 1);
    if (!(xfer_read_byte(io, &rx, 3000) && rx == ACK)) {
        /* Some receivers NAK the first EOT before ACKing the second. */
        xfer_write_bytes(io, &eot, 1);
        if (!(xfer_read_byte(io, &rx, 3000) && rx == ACK)) return ORION_XFER_ERR_PROTOCOL;
    }

    /* Empty block 0 ends the batch (we only ever send one file per call). */
    if (wait_for_crc_char(io, 5000, prog)) {
        uint8_t zero[128];
        memset(zero, 0, sizeof zero);
        (void)send_block(io, 0, zero, sizeof zero, prog);
    }
    return ORION_XFER_OK;
}

/* ── receive side ─────────────────────────────────────────────────────── */

/* Returns 1=EOT, 2=CAN, 3=got a valid block (out_block_num/out_len set,
 * out_data filled with exactly out_len bytes), 0=timeout/bad block (caller
 * should NAK and retry). */
static int recv_one_block(const orion_xfer_io_t* io, uint8_t* out_block_num,
                          uint8_t* out_data, size_t* out_len, uint32_t timeout_ms)
{
    uint8_t b;
    if (!xfer_read_byte(io, &b, timeout_ms)) return 0;
    if (b == EOT) return 1;
    if (b == CAN) return 2;
    if (b != SOH && b != STX) return 0;
    size_t dlen = (b == STX) ? 1024 : 128;

    uint8_t blk, cblk;
    if (!xfer_read_byte(io, &blk, 1000))  return 0;
    if (!xfer_read_byte(io, &cblk, 1000)) return 0;
    if ((uint8_t)(255 - blk) != cblk) return 0;

    for (size_t i = 0; i < dlen; ++i)
        if (!xfer_read_byte(io, &out_data[i], 1000)) return 0;

    uint8_t crc_hi, crc_lo;
    if (!xfer_read_byte(io, &crc_hi, 1000)) return 0;
    if (!xfer_read_byte(io, &crc_lo, 1000)) return 0;
    if (crc16_ccitt(out_data, dlen) != (uint16_t)(((uint16_t)crc_hi << 8) | crc_lo)) return 0;

    *out_block_num = blk;
    *out_len = dlen;
    return 3;
}

orion_xfer_result_t orion_ymodem_receive(const orion_xfer_io_t* io, const char* dest_dir,
                                         char* out_filename, size_t out_filename_sz,
                                         const orion_xfer_progress_t* prog)
{
    uint8_t data[1024];
    uint8_t block_num;
    size_t dlen;

    /* Phase 1: header block (filename + size). */
    uint32_t t0 = orion_millis();
    int got_header = 0;
    while (!got_header) {
        if (prog && prog->should_cancel && prog->should_cancel(prog->ctx)) return ORION_XFER_ERR_CANCELLED;
        uint8_t c = CRC_CHAR;
        xfer_write_bytes(io, &c, 1);
        int r = recv_one_block(io, &block_num, data, &dlen, 3000);
        if (r == 3 && block_num == 0) { got_header = 1; break; }
        if (r == 2) return ORION_XFER_ERR_CANCELLED;
        if (orion_millis() - t0 > 60000) return ORION_XFER_ERR_TIMEOUT;
    }

    char filename[96];
    /* Explicit precision (95 = sizeof(filename)-1), not a bare "%s": `data`
     * is a 1024-byte protocol buffer with no compiler-provable guarantee of
     * a null terminator within the first 96 bytes (it's YMODEM header data
     * from whatever's on the other end of the serial line), so GCC's
     * -Wformat-truncation can't prove this won't truncate and -Werror turns
     * that into a hard build failure. Runtime behavior is identical to the
     * bare "%s" this replaces -- snprintf already capped output at
     * sizeof(filename)-1 either way; this only makes that bound visible to
     * the compiler's own static analysis too. */
    snprintf(filename, sizeof filename, "%.95s", (const char*)data);
    if (filename[0] == '\0') {
        /* Empty header = sender ending an already-finished batch cleanly. */
        uint8_t ack = ACK;
        xfer_write_bytes(io, &ack, 1);
        return ORION_XFER_ERR_EMPTY_BATCH;
    }
    long filesize = -1;
    size_t namelen = strlen((const char*)data);
    if (namelen + 1 < dlen) filesize = atol((const char*)data + namelen + 1);

    {
        uint8_t ack = ACK;
        xfer_write_bytes(io, &ack, 1);
    }

    /* dest_dir may not exist yet (e.g. a first-time transfer into /sdcard/doom/
     * for the DOOM app) -- fopen() below would otherwise fail with no signal
     * beyond ORION_XFER_ERR_IO. mkdir() only needs to create the leaf; every
     * real caller in this codebase passes an existing parent (/sdcard itself,
     * or a dir the file manager just navigated into). EEXIST is the expected,
     * silent case. */
    errno = 0;
    (void)mkdir(dest_dir, 0777);

    char fullpath[256];
    /* Explicit precision: `dest_dir` is a caller-supplied parameter
     * (ultimately a console command argument), unbounded from the
     * compiler's view -- same -Wformat-truncation pattern as `filename`
     * above in this file. */
    snprintf(fullpath, sizeof fullpath, "%.159s/%.95s", dest_dir, filename);
    FILE* fp = fopen(fullpath, "wb");
    if (!fp) return ORION_XFER_ERR_IO;

    /* Phase 2: data blocks. */
    uint8_t expect = 1;
    size_t received = 0;
    for (;;) {
        uint8_t c = CRC_CHAR;
        xfer_write_bytes(io, &c, 1);
        int r = recv_one_block(io, &block_num, data, &dlen, 5000);
        if (r == 1) {
            uint8_t ack = ACK;
            xfer_write_bytes(io, &ack, 1);
            break;
        }
        if (r == 2) { fclose(fp); remove(fullpath); return ORION_XFER_ERR_CANCELLED; }
        if (r != 3) {
            uint8_t nak = NAK;
            xfer_write_bytes(io, &nak, 1);
            continue;
        }
        if (block_num == expect) {
            size_t take = dlen;
            if (filesize >= 0 && received + take > (size_t)filesize) take = (size_t)filesize - received;
            if (take > 0) fwrite(data, 1, take, fp);
            received += take;
            expect++;
            if (prog && prog->on_progress)
                prog->on_progress(prog->ctx, filename, received, filesize >= 0 ? (size_t)filesize : 0);
        } else if (block_num != (uint8_t)(expect - 1)) {
            /* Neither the expected block nor a resend of the last one --
             * out of sync, NAK and let the sender retry/resync. */
            uint8_t nak = NAK;
            xfer_write_bytes(io, &nak, 1);
            continue;
        }
        /* block_num == expect-1 is a duplicate (our previous ACK was lost)
         * -- already written, just re-ACK without rewriting. */
        uint8_t ack = ACK;
        xfer_write_bytes(io, &ack, 1);
        if (prog && prog->should_cancel && prog->should_cancel(prog->ctx)) {
            fclose(fp);
            remove(fullpath);
            return ORION_XFER_ERR_CANCELLED;
        }
    }
    fclose(fp);

    /* Phase 3: consume the closing empty block-0 so the sender's own EOT
     * handshake completes cleanly instead of timing out on its side. */
    {
        uint8_t c = CRC_CHAR;
        xfer_write_bytes(io, &c, 1);
        int r = recv_one_block(io, &block_num, data, &dlen, 2000);
        if (r == 3 && block_num == 0) {
            uint8_t ack = ACK;
            xfer_write_bytes(io, &ack, 1);
        }
    }

    snprintf(out_filename, out_filename_sz, "%s", filename);
    return ORION_XFER_OK;
}
