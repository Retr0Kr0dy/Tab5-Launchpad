/*
 * sic_espidf_camera.c — Tab5 MIPI-CSI camera, a thin transport wrapper over
 * ESP-IDF's esp_video / esp_cam_sensor managed components.
 *
 * This is the one place in the Tab5 bring-up where depending on a substantial
 * third-party ESP-IDF component is explicitly authorised. The containment rule that comes with that
 * authorisation is honoured strictly: every esp_video / V4L2 / esp_cam_sensor
 * type, ioctl constant and device-node path is confined to this file.
 * include/sic/video/camera.h stays vendor-neutral, so the component behind it
 * can be swapped without touching a single caller.
 *
 * ── What the component actually does for us ───────────────────────────────
 * Camera-sensor *identification* is not this driver's job. esp_video_init()
 * walks a linker-section array of registered detect functions
 * (__esp_cam_sensor_detect_fn_array_start/end in esp_video_init.c) and calls
 * each one over SCCB; whichever of Tab5's five supported sensors (sc2336,
 * sc202cs, sc101iot, sc035hgs, sc030iot) is fitted answers and gets bound to
 * /dev/video0. We hand it a sensor-agnostic CSI config and nothing more.
 * Which detect functions are compiled in is a Kconfig choice belonging to the
 * firmware image, not to this file — note that esp_video_init() hard-fails
 * if *any* enabled CSI detect function finds no sensor, so exactly one
 * sensor should be enabled in sdkconfig for a given board.
 *
 * SCCB (the I2C channel the sensor is configured over) rides Tab5's internal
 * I2C bus, which SIC already owns. Rather than let esp_video open a second
 * master on the same port (which would collide with i2c_bus_espidf.c's
 * handle), we pass init_sccb = false plus the live bus handle, exactly as
 * M5Stack's own hal_camera.cpp does with bsp_i2c_get_handle(). SIC's public
 * sic_i2c_* contract deliberately hands out no raw handles, so we retrieve
 * the handle from the driver itself via ESP-IDF's own
 * i2c_master_get_bus_handle() (driver/i2c_master.h, IDF >= 5.4) after making
 * sure the bus is open, rather than exposing i2c_bus_espidf.c's internal
 * table to a second caller.
 *
 * ── Verification note ─────────────────────────────────────────────────────
 * esp_video is not part of the base ESP-IDF SDK; the source used to verify
 * this file is the copy vendored in M5Stack's own reference firmware at
 * M5Tab5-UserDemo/platforms/tab5/components/esp_video. Every ioctl constant,
 * struct name and struct field used below was confirmed to exist in that
 * tree's real headers (include/linux/videodev2.h, include/esp_video_init.h,
 * include/esp_video_device.h, include/sys/mman.h) and the call *order* was
 * taken from its own examples/capture_stream/main/capture_stream_main.c and
 * from M5Stack's hal_camera.cpp.
 *
 * ── Known limitation: get_frame() timeout ─────────────────────────────────
 * sic_cam_get_frame()'s timeout_ms cannot be honoured. esp_video's
 * VIDIOC_DQBUF implementation (esp_video_ioctl_dqbuf in src/esp_video_ioctl.c)
 * hardcodes `uint32_t ticks = portMAX_DELAY;` with no way to override it, and
 * its VFS table (s_esp_video_vfs in src/esp_video_vfs.c) registers no
 * select/poll handler, so select() on the device fd is not available either.
 * The parameter is therefore accepted and ignored, and get_frame() blocks
 * until a frame arrives or the stream errors. Callers needing a bounded wait
 * must run capture on its own task. Flagged here rather than faked.
 */
#if defined(SIC_BACKEND_ESPIDF)

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <time.h>       /* struct timespec/timeval, used by struct v4l2_buffer */

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "esp_err.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"

#include "linux/videodev2.h"
#include "esp_video_init.h"
#include "esp_video_device.h"

#include "sic/sic.h"
#include "sic/sic_registry.h"
#include "sic/video/camera.h"
#include "sic/bus/i2c_bus.h"
#include "sic/bus/delay.h"
#include "boards/tab5/ioexpander.h"

/* CAM_RST: expander 0, bit 6 — per boards/tab5/ioexpander.h's documented bit
 * map ("HP_DET(in,b7), CAM_RST(out,b6), TP_RST(out,b5), ..."). Driven here,
 * not carried in sic_camera_cfg_t, because it is a shared-I2C-expander bit
 * rather than a GPIO. */
#define TAB5_CAM_RST_EXP        0
#define TAB5_CAM_RST_BIT        6

/*
 * LEDC camera clock.
 *
 * Timer/channel: the reference (bsp_cam_osc_init() in m5stack_tab5.c) uses
 * LEDC_TIMER_0 + LEDC_CHANNEL_0 — but that same file's backlight code
 * (bsp_display_brightness_init()) *also* claims LEDC_TIMER_0, at 12-bit
 * resolution and a PWM-rate frequency. Two incompatible configurations on one
 * timer is a genuine defect in the reference: whichever runs last silently
 * destroys the other's output. We keep LEDC_CHANNEL_0 (channels do not
 * conflict) but move the camera clock to LEDC_TIMER_1 so a Tab5 backlight
 * implementation can keep the reference's LEDC_TIMER_0 without breaking XCLK.
 *
 * Duty resolution: 1 bit, matching the reference — and this is not the
 * oddity it first looks like. ESP32-P4's LEDC sources are XTAL 40 MHz,
 * PLL_F80M 80 MHz and RC_FAST (SOC_LEDC_CLKS in soc/esp32p4/clk_tree_defs.h),
 * so the fastest available source is 80 MHz. LEDC's output frequency is
 * src_hz / (divider * 2^duty_res) with divider >= 1, so at 24 MHz the counter
 * gets at most 80/24 = 3.33 counts per period: 2^1 = 2 counts fits, 2^2 = 4
 * does not. 1 bit is the *only* resolution that can produce 24 MHz on this
 * SoC, and duty = 1 of 2 gives the 50 % square wave a sensor XCLK wants.
 * Kept deliberately, not copied blindly.
 */
#define TAB5_CAM_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define TAB5_CAM_LEDC_TIMER     LEDC_TIMER_1
#define TAB5_CAM_LEDC_CHANNEL   LEDC_CHANNEL_0
#define TAB5_CAM_LEDC_DUTY_RES  LEDC_TIMER_1_BIT
#define TAB5_CAM_LEDC_DUTY      1   /* 1 of 2^1 -> 50 % */

/* SCCB clock. Matches the reference's csi_config.sccb_config.freq and Tab5's
 * internal-bus rate (TAB5_I2C_HZ). */
#define TAB5_CAM_SCCB_FREQ_HZ   400000u

/* Reset pulse timings. No datasheet value covers all five candidate sensors,
 * so these are conservative round numbers: hold reset low well past the
 * XCLK becoming stable, then give the sensor time to boot before SCCB. */
#define TAB5_CAM_RST_LOW_MS     10
#define TAB5_CAM_RST_SETTLE_MS  20

/* Ring depth. Two is what both the reference example and M5Stack's own
 * hal_camera.cpp use: enough to keep the CSI DMA fed while one frame is
 * borrowed by the caller. */
#define TAB5_CAM_BUF_COUNT      2

/* V4L2 buffer type/memory model used throughout. MMAP (rather than USERPTR)
 * lets the driver own and align the frame memory itself. */
#define TAB5_CAM_BUF_TYPE       V4L2_BUF_TYPE_VIDEO_CAPTURE
#define TAB5_CAM_MEMORY         V4L2_MEMORY_MMAP

typedef struct {
    sic_camera_cfg_t cfg;

    int      fd;                            /* -1 when closed */
    bool     clk_on;
    bool     streaming;
    uint32_t seq;                           /* our own frame counter, see get_frame */

    int      nbuf;
    void*    buf[TAB5_CAM_BUF_COUNT];
    size_t   buf_len[TAB5_CAM_BUF_COUNT];
} tab5_cam_ctx_t;

static tab5_cam_ctx_t g_ctx;
static camera_t       g_cam;

/* esp_video has no deinit entry point (confirmed: no esp_video_deinit anywhere
 * in the component), so initialisation is strictly one-shot for the lifetime
 * of the process — stop()/start() cycles must not repeat it. */
static bool g_video_inited = false;

/* ── Camera master clock (LEDC) ───────────────────────────────────────────── */

static int cam_clk_start(tab5_cam_ctx_t* c)
{
    if (c->cfg.pin_clk < 0) return SIC_OK;   /* externally clocked board */
    if (c->clk_on) return SIC_OK;

    ledc_timer_config_t tcfg = {0};
    tcfg.speed_mode      = TAB5_CAM_LEDC_MODE;
    tcfg.duty_resolution = TAB5_CAM_LEDC_DUTY_RES;
    tcfg.timer_num       = TAB5_CAM_LEDC_TIMER;
    tcfg.freq_hz         = c->cfg.clk_hz;
    /* Must NOT be LEDC_AUTO_CLK: on ESP32-P4 there is no per-timer clock mux
     * (SOC_LEDC_HAS_TIMER_SPECIFIC_MUX is unset) -- every low-speed LEDC
     * timer shares one global slow clock (esp_driver_ledc/src/ledc.c's
     * ledc_set_timer_div(), the p_ledc_obj[speed_mode]->glb_clk check).
     * This call fails with "timer clock conflict, already is X but attempt
     * to Y" whenever the backlight timer (5kHz/12-bit, main/display.c) has
     * already run its own LEDC_AUTO_CLK selection first, since AUTO
     * independently picks a source per timer and the two frequency/
     * resolution combos don't auto-select the same one. Pinning both this
     * timer and the backlight's to the same explicit LEDC_USE_PLL_DIV_CLK
     * (80MHz PLL_F80M) makes them agree regardless of init order --
     * required anyway since 24MHz @ 1-bit
     * is only reachable from the 80MHz source (see comment above). */
    tcfg.clk_cfg         = LEDC_USE_PLL_DIV_CLK;
    tcfg.deconfigure     = false;
    /* ledc_timer_config() internally resumes + resets the timer, so this also
     * un-pauses a timer left paused by a previous cam_clk_stop(). */
    if (ledc_timer_config(&tcfg) != ESP_OK) return SIC_EIO;

    ledc_channel_config_t ccfg = {0};
    ccfg.gpio_num   = c->cfg.pin_clk;
    ccfg.speed_mode = TAB5_CAM_LEDC_MODE;
    ccfg.channel    = TAB5_CAM_LEDC_CHANNEL;
    ccfg.intr_type  = LEDC_INTR_DISABLE;
    ccfg.timer_sel  = TAB5_CAM_LEDC_TIMER;
    ccfg.duty       = TAB5_CAM_LEDC_DUTY;
    ccfg.hpoint     = 0;
    ccfg.sleep_mode = LEDC_SLEEP_MODE_KEEP_ALIVE;
    if (ledc_channel_config(&ccfg) != ESP_OK) return SIC_EIO;

    c->clk_on = true;
    return SIC_OK;
}

static void cam_clk_stop(tab5_cam_ctx_t* c)
{
    if (!c->clk_on) return;
    /* Park the pin low and halt the timer. The timer is only paused, not
     * de-configured: deconfiguring requires it be unbound from every channel
     * first, and pausing is enough to stop the clock. */
    ledc_stop(TAB5_CAM_LEDC_MODE, TAB5_CAM_LEDC_CHANNEL, 0);
    ledc_timer_pause(TAB5_CAM_LEDC_MODE, TAB5_CAM_LEDC_TIMER);
    c->clk_on = false;
}

/* ── esp_video one-time bring-up ──────────────────────────────────────────── */

static int cam_video_init_once(void)
{
    if (g_video_inited) return SIC_OK;

    /* Idempotent — a no-op if board preinit (tab5_ioexp_init) already opened
     * bus 0, but it guarantees the port exists before we ask for its handle. */
    sic_i2c_begin_bus(TAB5_I2C_BUS, TAB5_I2C_SDA, TAB5_I2C_SCL, TAB5_I2C_HZ);

    i2c_master_bus_handle_t i2c = NULL;
    if (i2c_master_get_bus_handle((i2c_port_num_t)TAB5_I2C_BUS, &i2c) != ESP_OK || !i2c)
        return SIC_EIO;

    /* reset_pin/pwdn_pin = -1: the sensor's reset is an expander bit we drive
     * ourselves (see cam_start), not a GPIO esp_video can toggle. */
    esp_video_init_csi_config_t csi = {
        .sccb_config = {
            /* .i2c_handle is a member of the config's anonymous union; with
             * init_sccb = false esp_video takes this handle as-is instead of
             * creating a second master on the same port. Same initialiser
             * shape M5Stack's hal_camera.cpp uses. */
            .init_sccb  = false,
            .i2c_handle = i2c,
            .freq       = TAB5_CAM_SCCB_FREQ_HZ,
        },
        .reset_pin = -1,
        .pwdn_pin  = -1,
    };

    esp_video_init_config_t vcfg = {
        .csi  = &csi,
        .dvp  = NULL,
        .jpeg = NULL,
        .isp  = NULL,   /* NULL => component's default IPA set */
    };

    if (esp_video_init(&vcfg) != ESP_OK) return SIC_EIO;

    g_video_inited = true;
    return SIC_OK;
}

/* ── Buffer ring ──────────────────────────────────────────────────────────── */

static void cam_unmap_all(tab5_cam_ctx_t* c)
{
    for (int i = 0; i < c->nbuf; i++) {
        if (c->buf[i]) munmap(c->buf[i], c->buf_len[i]);
        c->buf[i]     = NULL;
        c->buf_len[i] = 0;
    }
    c->nbuf = 0;
}

/* REQBUFS + (QUERYBUF, mmap, QBUF) per slot. Order taken verbatim from
 * esp_video's own capture_stream example. */
static int cam_setup_buffers(tab5_cam_ctx_t* c)
{
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count  = TAB5_CAM_BUF_COUNT;
    req.type   = TAB5_CAM_BUF_TYPE;
    req.memory = TAB5_CAM_MEMORY;
    if (ioctl(c->fd, VIDIOC_REQBUFS, &req) != 0) return SIC_EIO;

    for (int i = 0; i < TAB5_CAM_BUF_COUNT; i++) {
        struct v4l2_buffer vb;
        memset(&vb, 0, sizeof(vb));
        vb.type   = TAB5_CAM_BUF_TYPE;
        vb.memory = TAB5_CAM_MEMORY;
        vb.index  = (uint32_t)i;
        if (ioctl(c->fd, VIDIOC_QUERYBUF, &vb) != 0) return SIC_EIO;

        void* p = mmap(NULL, vb.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                       c->fd, (off_t)vb.m.offset);
        if (!p) return SIC_ENOMEM;

        c->buf[i]     = p;
        c->buf_len[i] = (size_t)vb.length;
        c->nbuf       = i + 1;

        if (ioctl(c->fd, VIDIOC_QBUF, &vb) != 0) return SIC_EIO;
    }
    return SIC_OK;
}

/* ── camera_vtbl_s ────────────────────────────────────────────────────────── */

static int cam_start(const void* self, sic_cam_format_t* fmt)
{
    const camera_t* cam = (const camera_t*)self;
    tab5_cam_ctx_t* c = cam ? (tab5_cam_ctx_t*)cam->impl : NULL;
    if (!c) return SIC_EINVAL;
    if (c->streaming) return SIC_OK;

    int rc;

    /* A *second* start() after a stop() produces a garbage/noise frame,
     * while the first-ever start() is fine. Root cause -- the CAM_RST
     * hardware-reset pulse below (steps 1-3) ran
     * unconditionally on every start(), but the actual sensor *configuration*
     * (cam_video_init_once() -> esp_video_init(), which probes the sensor
     * over SCCB and programs its format/mode registers) is a true one-shot,
     * gated on g_video_inited, because esp_video itself has no deinit entry
     * point (see that function's own comment). So every start() after the
     * first hard-reset the sensor back to its power-on-default register
     * state, then skipped the only step that would have reprogrammed it --
     * STREAMON then proceeded against a sensor whose format/mode was never
     * set up, hence noise. Fix: only pulse CAM_RST (and therefore only
     * require re-configuration) on the one call that actually needs it --
     * the first one, same lifetime as g_video_inited itself. The clock
     * still needs restarting every time (cam_stop() parks it), just not the
     * reset pulse. */
    int first_ever = !g_video_inited;

    if (first_ever) {
        /* 1. Hold the sensor in reset while its clock comes up. */
        tab5_ioexp_set(TAB5_CAM_RST_EXP, TAB5_CAM_RST_BIT, 0);
    }

    /* 2. XCLK first — the sensor needs a running master clock to boot. */
    rc = cam_clk_start(c);
    if (rc != SIC_OK) goto fail_reset;

    if (first_ever) {
        sic_delay_ms(TAB5_CAM_RST_LOW_MS);

        /* 3. Release reset and let the sensor come up before SCCB traffic. */
        tab5_ioexp_set(TAB5_CAM_RST_EXP, TAB5_CAM_RST_BIT, 1);
        sic_delay_ms(TAB5_CAM_RST_SETTLE_MS);
    }

    /* 4. Probe + bind the sensor and register /dev/video0 (once ever). */
    rc = cam_video_init_once();
    if (rc != SIC_OK) goto fail_clk;

    /* 5. Open the CSI capture node. */
    c->fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
    if (c->fd < 0) { rc = SIC_EIO; goto fail_clk; }

    /* 6. Read the sensor's default geometry, then apply the caller's
     *    overrides. Zero width/height/fourcc mean "keep the default". */
    struct v4l2_format cur;
    memset(&cur, 0, sizeof(cur));
    cur.type = TAB5_CAM_BUF_TYPE;
    if (ioctl(c->fd, VIDIOC_G_FMT, &cur) != 0) { rc = SIC_EIO; goto fail_fd; }

    struct v4l2_format want;
    memset(&want, 0, sizeof(want));
    want.type                = TAB5_CAM_BUF_TYPE;
    want.fmt.pix.width       = (fmt && fmt->width)  ? fmt->width  : cur.fmt.pix.width;
    want.fmt.pix.height      = (fmt && fmt->height) ? fmt->height : cur.fmt.pix.height;
    want.fmt.pix.pixelformat = (fmt && fmt->fourcc) ? fmt->fourcc : cur.fmt.pix.pixelformat;

    if (want.fmt.pix.width       != cur.fmt.pix.width  ||
        want.fmt.pix.height      != cur.fmt.pix.height ||
        want.fmt.pix.pixelformat != cur.fmt.pix.pixelformat) {
        if (ioctl(c->fd, VIDIOC_S_FMT, &want) != 0) { rc = SIC_EINVAL; goto fail_fd; }
    }

    /* Report back what actually ended up negotiated -- see the vtable
     * comment in camera.h for why callers need this. */
    if (fmt) {
        fmt->width  = (uint16_t)want.fmt.pix.width;
        fmt->height = (uint16_t)want.fmt.pix.height;
        fmt->fourcc = (uint32_t)want.fmt.pix.pixelformat;
    }

    /* 7. Allocate, map and enqueue the ring. */
    rc = cam_setup_buffers(c);
    if (rc != SIC_OK) goto fail_bufs;

    /* 8. Go. STREAMON takes a pointer to the buffer *type*, not a struct. */
    int type = TAB5_CAM_BUF_TYPE;
    if (ioctl(c->fd, VIDIOC_STREAMON, &type) != 0) { rc = SIC_EIO; goto fail_bufs; }

    c->seq       = 0;
    c->streaming = true;
    return SIC_OK;

fail_bufs:
    cam_unmap_all(c);
fail_fd:
    close(c->fd);
    c->fd = -1;
fail_clk:
    cam_clk_stop(c);
fail_reset:
    tab5_ioexp_set(TAB5_CAM_RST_EXP, TAB5_CAM_RST_BIT, 0);
    return rc;
}

static int cam_stop(const void* self)
{
    const camera_t* cam = (const camera_t*)self;
    tab5_cam_ctx_t* c = cam ? (tab5_cam_ctx_t*)cam->impl : NULL;
    if (!c) return SIC_EINVAL;

    if (c->fd >= 0) {
        if (c->streaming) {
            int type = TAB5_CAM_BUF_TYPE;
            ioctl(c->fd, VIDIOC_STREAMOFF, &type);
        }
        cam_unmap_all(c);
        close(c->fd);
        c->fd = -1;
    }
    c->streaming = false;

    /* Just kill the clock -- do NOT put the sensor back into hardware
     * reset. Resetting it here breaks the *next* start(): esp_video's
     * sensor configuration
     * (cam_video_init_once() -> esp_video_init()) only ever runs once per
     * process (no deinit entry point), so a sensor reset here would wipe
     * register state that start() no longer re-programs after its first
     * call (see that function's own comment for the full explanation).
     * Leaving the sensor un-reset but unclocked between start()/stop()
     * cycles is what keeps its one-time configuration valid for the next
     * start(). esp_video itself stays initialised regardless (the
     * component offers no way to undo that either). */
    cam_clk_stop(c);
    return SIC_OK;
}

static int cam_get_frame(const void* self, void** buf, size_t* len,
                         uint32_t* seq, int timeout_ms)
{
    /* timeout_ms is accepted and ignored — see the file header: esp_video's
     * DQBUF is unconditionally portMAX_DELAY and its VFS exposes no
     * select()/poll() handler, so there is no wait to bound. */
    (void)timeout_ms;

    const camera_t* cam = (const camera_t*)self;
    tab5_cam_ctx_t* c = cam ? (tab5_cam_ctx_t*)cam->impl : NULL;
    if (!c) return SIC_EINVAL;
    if (!c->streaming || c->fd < 0) return SIC_EIO;

    struct v4l2_buffer vb;
    memset(&vb, 0, sizeof(vb));
    vb.type   = TAB5_CAM_BUF_TYPE;
    vb.memory = TAB5_CAM_MEMORY;
    if (ioctl(c->fd, VIDIOC_DQBUF, &vb) != 0) return SIC_EIO;

    if ((int)vb.index >= c->nbuf || !c->buf[vb.index]) return SIC_EIO;

    if (buf) *buf = c->buf[vb.index];
    if (len) *len = (size_t)vb.bytesused;
    /* esp_video's dqbuf sets index/bytesused/flags/m.userptr but never
     * vb.sequence, so the frame counter is ours to maintain. */
    if (seq) *seq = ++c->seq;
    else     c->seq++;

    return SIC_OK;
}

static int cam_release_frame(const void* self, void* buf)
{
    const camera_t* cam = (const camera_t*)self;
    tab5_cam_ctx_t* c = cam ? (tab5_cam_ctx_t*)cam->impl : NULL;
    if (!c || !buf) return SIC_EINVAL;
    if (c->fd < 0) return SIC_EIO;

    int index = -1;
    for (int i = 0; i < c->nbuf; i++) {
        if (c->buf[i] == buf) { index = i; break; }
    }
    if (index < 0) return SIC_EINVAL;   /* not one of ours */

    /* For V4L2_MEMORY_MMAP a re-queue only needs type/memory/index; the
     * driver already knows where that slot lives. */
    struct v4l2_buffer vb;
    memset(&vb, 0, sizeof(vb));
    vb.type   = TAB5_CAM_BUF_TYPE;
    vb.memory = TAB5_CAM_MEMORY;
    vb.index  = (uint32_t)index;
    return (ioctl(c->fd, VIDIOC_QBUF, &vb) == 0) ? SIC_OK : SIC_EIO;
}

static const struct camera_vtbl_s TAB5_CAM_VT = {
    cam_start,
    cam_stop,
    cam_get_frame,
    cam_release_frame
};

/* ── Registration ─────────────────────────────────────────────────────────── */

/* probe() does zero I/O, per docs/DESIGN_INVARIANTS.md: it validates the hint,
 * copies the cfg and publishes the vtable. Every clock, GPIO-expander and
 * esp_video interaction happens in start(). */
static int probe_tab5_cam_espidf(const void* icdesc, void** out)
{
    const sic_board_ic_t* d = (const sic_board_ic_t*)icdesc;
    if (!d || !d->hint || strcmp(d->hint, "tab5_cam_espidf") != 0 || !d->cfg) return -1;

    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.cfg = *(const sic_camera_cfg_t*)d->cfg;
    g_ctx.fd  = -1;
    if (g_ctx.cfg.pin_clk >= 0 && g_ctx.cfg.clk_hz == 0) return -1;

    g_cam.v    = &TAB5_CAM_VT;
    g_cam.impl = &g_ctx;
    *out = &g_cam;
    return 0;
}

static const sic_driver_t DRV_TAB5_CAM_ESPIDF = {
    "tab5_cam_espidf", SIC_F_CAMERA, probe_tab5_cam_espidf, NULL
};

void sic_register_driver_tab5_cam_espidf(void)
{
    sic_registry_register(&DRV_TAB5_CAM_ESPIDF);
}

#endif /* SIC_BACKEND_ESPIDF */
