#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Generic byte-level transport: the YMODEM engine below only ever talks to
 * a file through this pair of non-blocking read/write functions -- same
 * "bytes actually transferred, 0 if none ready" contract already used by
 * orion_console_read()/write(). A serial backend is the only one wired up
 * today (see kOrionSerialXferIo in touchmenu.c); a BLE or Wi-Fi backend
 * later is a matter of supplying a different read/write pair, not touching
 * this engine at all. */
typedef struct {
    size_t (*read)(void* ctx, uint8_t* buf, size_t len);
    size_t (*write)(void* ctx, const uint8_t* buf, size_t len);
    void* ctx;
} orion_xfer_io_t;

typedef enum {
    ORION_XFER_OK = 0,
    ORION_XFER_ERR_TIMEOUT,
    ORION_XFER_ERR_CANCELLED,
    ORION_XFER_ERR_IO,
    ORION_XFER_ERR_PROTOCOL,
    ORION_XFER_ERR_EMPTY_BATCH, /* receive-only: sender ended the batch with no file */
} orion_xfer_result_t;

typedef struct {
    /* Polled at least once per block in both directions; return nonzero to
     * abort cleanly (device sends CAN, or the loop just stops). */
    int (*should_cancel)(void* ctx);
    /* done/total in bytes; total is 0 on the receive side until the header
     * block has actually arrived (the size isn't known before that). */
    void (*on_progress)(void* ctx, const char* filename, size_t done, size_t total);
    void* ctx;
} orion_xfer_progress_t;

/* Sends one file (device -> other end). The other end must be a YMODEM
 * *receiver* (e.g. `rz --ymodem` on a PC, or a terminal's "receive file"
 * feature). Blocks the calling task until done/cancelled/timed out --
 * callers on Orion's single UI task should only invoke this from a
 * dedicated screen, not from inside another screen's own poll loop. */
orion_xfer_result_t orion_ymodem_send(const orion_xfer_io_t* io, const char* filepath,
                                      const orion_xfer_progress_t* prog);

/* Receives one file (other end -> device) into dest_dir, using whatever
 * filename the sender provides. The other end must be a YMODEM *sender*
 * (e.g. `sz --ymodem <file>` on a PC, or a terminal's "send file" feature).
 * out_filename receives the real name the sender sent, since the caller
 * doesn't know it in advance. */
orion_xfer_result_t orion_ymodem_receive(const orion_xfer_io_t* io, const char* dest_dir,
                                         char* out_filename, size_t out_filename_sz,
                                         const orion_xfer_progress_t* prog);

#ifdef __cplusplus
}
#endif
