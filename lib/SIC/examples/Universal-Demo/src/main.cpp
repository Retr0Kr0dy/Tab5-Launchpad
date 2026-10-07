#include <Arduino.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
  #include "sic/sic.h"
  #include "sic/audio/mic.h"
  #include "sic/audio/amp.h"
  #include "sic/input/encoder.h"
  #include "sic/power/battery.h"
  #include "sic/storage/sd.h"
}

static void print_help();
static void handle_command(char c);

static const char* yesno(int v) { return v ? "yes" : "no"; }

static void print_sys_info() {
  sic_sysinfo_t si;
  memset(&si, 0, sizeof(si));
  if (sic_sysinfo(&si) != 0) {
    Serial.println("[SYS] unavailable");
    return;
  }

  char mac[18];
  snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
           si.mac[0], si.mac[1], si.mac[2], si.mac[3], si.mac[4], si.mac[5]);

  Serial.printf("[SYS] model=%s rev=%u cpu=%uMHz flash=%uB@%uHz psram=%uB mac=%s\n",
                si.chip_model ? si.chip_model : "?",
                (unsigned)si.chip_rev,
                (unsigned)si.cpu_mhz,
                (unsigned)si.flash_bytes,
                (unsigned)si.flash_hz,
                (unsigned)si.psram_bytes,
                mac);
}

static void print_caps() {
  Serial.println("[SIC] capabilities:");
  Serial.printf("  keyboard: %s (%d)\n", yesno(sic_has(SIC_CAP_KSCAN)),     sic_count_cap(SIC_CAP_KSCAN));
  Serial.printf("  mic:      %s (%d)\n", yesno(sic_has(SIC_CAP_MIC)),       sic_count_cap(SIC_CAP_MIC));
  Serial.printf("  amp:      %s (%d)\n", yesno(sic_has(SIC_CAP_AUDIO_AMP)), sic_count_cap(SIC_CAP_AUDIO_AMP));
  Serial.printf("  charger:  %s (%d)\n", yesno(sic_has(SIC_CAP_CHARGER)),   sic_count_cap(SIC_CAP_CHARGER));
  Serial.printf("  ir:       %s (%d)\n", yesno(sic_has(SIC_CAP_IR_TX)),     sic_count_cap(SIC_CAP_IR_TX));
  Serial.printf("  sd:       %s (%d)\n", yesno(sic_has(SIC_CAP_SD)),        sic_count_cap(SIC_CAP_SD));

  for (int fn = 0; fn < SIC_F__COUNT; ++fn) {
    int n = sic_count_fn((sic_func_id_t)fn);
    if (!n) continue;
    for (int i = 0; i < n; ++i) {
      const char* name = sic_name_fn((sic_func_id_t)fn, i);
      Serial.printf("  driver[%s:%d] = %s\n", sic_func_name((sic_func_id_t)fn), i, name ? name : "?");
    }
  }
}

static void print_battery() {
  sic_battery_t bat;
  memset(&bat, 0, sizeof(bat));
  int rc = sic_battery_read(&bat);
  if (rc == 0) {
    Serial.printf("[BAT] %.3f V  %d%%\n", (double)bat.voltage_v, bat.percent);
  } else if (rc == SIC_ENOENT) {
    Serial.println("[BAT] unsupported on this target");
  } else {
    Serial.printf("[BAT] read error rc=%d\n", rc);
  }
}


static const char* charger_state_name(sic_chg_state_t st) {
  switch (st) {
    case SIC_CHG_NOT_PRESENT: return "not-present/not-charging";
    case SIC_CHG_CHARGING:    return "charging";
    case SIC_CHG_FULL:        return "full";
    case SIC_CHG_FAULT:       return "fault";
    default:                  return "?";
  }
}

static void print_charger() {
  sic_chg_state_t st = SIC_CHG_NOT_PRESENT;
  int rc = sic_charger_state(&st);
  if (rc == 0) Serial.printf("[CHG] %s (%d)\n", charger_state_name(st), (int)st);
  else         Serial.printf("[CHG] unsupported/error rc=%d\n", rc);
}

static void scan_i2c() {
  uint8_t addrs[32];
  int n = sic_i2c_scan(addrs, (int)sizeof(addrs));
  if (n < 0) {
    Serial.printf("[I2C] scan error rc=%d\n", n);
    return;
  }
  if (n == 0) {
    Serial.println("[I2C] no devices found");
    return;
  }
  Serial.printf("[I2C] %d device(s):", n);
  for (int i = 0; i < n; ++i) Serial.printf(" 0x%02X", addrs[i]);
  Serial.println();
}

static float vu_level_mono(const int16_t* samples, int n) {
  if (!samples || n <= 0) return 0.0f;
  int64_t sum = 0;
  for (int i = 0; i < n; ++i) sum += samples[i];
  float mean = (float)sum / (float)n;
  double acc = 0.0;
  for (int i = 0; i < n; ++i) {
    float v = (float)samples[i] - mean;
    acc += (double)v * (double)v;
  }
  float rms = sqrtf((float)(acc / (double)n));
  float vu = rms / 2048.0f;
  if (vu < 0.0f) vu = 0.0f;
  if (vu > 1.0f) vu = 1.0f;
  return vu;
}

static void record_mic_then_playback() {
  const int sample_rate = 16000;
  const int seconds = 5;
  const size_t total_frames = (size_t)sample_rate * (size_t)seconds;

  const mic_t* mic = sic_mic(0);
  if (!mic || !mic->v || !mic->v->start || !mic->v->read) {
    Serial.println("[MIC] unsupported on this target");
    return;
  }

  const amp_t* amp = sic_amp(0);
  if (!amp || !amp->v || !amp->v->play_mono) {
    Serial.println("[SPK] no playback-capable amp on this target");
    return;
  }

  /* Keep the 5-second recorder buffer persistent.  Repeatedly malloc/free'ing
   * 160 KB after I2S DMA starts fragments the ESP32-S3 heap quickly, which made
   * the second `m` test fail even though total RAM was still available.
   */
  static int16_t* rec = nullptr;
  static size_t rec_cap_frames = 0;
  if (rec_cap_frames < total_frames) {
    int16_t* next = (int16_t*)realloc(rec, total_frames * sizeof(int16_t));
    if (!next) {
      Serial.printf("[MIC] allocation failed (%u bytes)\n", (unsigned)(total_frames * sizeof(int16_t)));
      return;
    }
    rec = next;
    rec_cap_frames = total_frames;
  }
  memset(rec, 0, total_frames * sizeof(int16_t));

  int rc = mic->v->start(mic, sample_rate);
  if (rc != 0) {
    Serial.printf("[MIC] start error rc=%d\n", rc);
    return;
  }

  Serial.printf("[MIC] recording %d seconds @ %d Hz...\n", seconds, sample_rate);
  size_t got = 0;
  unsigned long deadline = millis() + (unsigned long)(seconds * 1000 + 1200);
  while (got < total_frames && millis() < deadline) {
    size_t want = total_frames - got;
    if (want > 512) want = 512;
    int n = mic->v->read(mic, rec + got, (int)want);
    if (n > 0) {
      got += (size_t)n;
    } else {
      delay(1);
    }
  }
  sic_mic_close();

  if (got == 0) {
    Serial.println("[MIC] no samples captured");
    return;
  }

  sic_audio_postprocess(rec, got);
  Serial.printf("[MIC] captured=%u frames vu=%.3f\n", (unsigned)got, (double)vu_level_mono(rec, (int)got));
  Serial.println("[SPK] playing captured audio...");
  rc = amp->v->play_mono(amp, rec, got, sample_rate);
  if (rc < 0) Serial.printf("[SPK] playback error rc=%d\n", rc);
  else        Serial.printf("[SPK] playback done frames=%d\n", rc);

}

static void beep_once() {
  const amp_t* amp = sic_amp(0);
  int rc = -1;
  if (amp && amp->v && amp->v->beep_ms) rc = amp->v->beep_ms(amp, 2000);
  else rc = sic_spk_beep_1khz_ms(2000);
  if (rc == 0) Serial.println("[SPK] beep ok (2s, stopped)");
  else         Serial.printf("[SPK] unsupported/error rc=%d\n", rc);
}

static void test_ir() {
  int rc = sic_ir_send_nec(0x00FF00FFu);
  if (rc == 0) Serial.println("[IR] sent NEC 0x00FF00FF");
  else         Serial.printf("[IR] unsupported/error rc=%d\n", rc);
}

static void print_sd() {
  const sd_t* sd = sic_sd(0);
  if (!sd || !sd->v) {
    Serial.println("[SD] unsupported on this target");
    return;
  }
  int present = sd->v->present ? sd->v->present(sd) : sic_sd_present();
  if (!present) {
    Serial.println("[SD] no card / mount failed");
    return;
  }
  uint64_t bytes = sd->v->card_size_bytes ? sd->v->card_size_bytes(sd) : 0;
  Serial.printf("[SD] mounted, size=%llu bytes\n", (unsigned long long)bytes);
}

static void print_key_event(const sic_key_event_t* ev) {
  if (!ev) return;
  Serial.printf("[KEY] %s code=%u ascii=",
                ev->pressed ? "down" : "up",
                (unsigned)ev->code);
  if (ev->ascii && isprint((unsigned char)ev->ascii)) Serial.printf("'%c'", ev->ascii);
  else if (ev->ascii)                                Serial.printf("0x%02X", (unsigned char)ev->ascii);
  else                                               Serial.print("-");
  Serial.printf(" mods[S=%u C=%u A=%u O=%u F=%u Caps=%u]\n",
                ev->shift, ev->ctrl, ev->alt, ev->opt, ev->fn, ev->caps);
}

static void poll_keyboard() {
  sic_key_event_t ev;
  while (sic_key_poll(&ev) > 0) {
    print_key_event(&ev);
    if (ev.pressed && ev.ascii) handle_command(ev.ascii);
  }
}

static void poll_encoder() {
  const encoder_t* enc = sic_encoder(0);
  if (!enc || !enc->v) return;

  if (enc->v->read_delta) {
    int d = enc->v->read_delta(enc);
    if (d) Serial.printf("[ENC] delta=%d\n", d);
  }

  static int last_btn = -2;
  if (enc->v->read_btn) {
    int b = enc->v->read_btn(enc);
    if (b != last_btn) {
      last_btn = b;
      if (b >= 0) Serial.printf("[ENC] button=%s\n", b ? "down" : "up");
    }
  }
}

static void handle_command(char c) {
  switch (c) {
    case '?': print_help();      break;
    case 's': print_sys_info();  break;
    case 'd': print_caps();      break;
    case 'b': print_battery();   break;
    case 'c': print_charger();   break;
    case 'i': scan_i2c();        break;
    case 'm': record_mic_then_playback(); break;
    case 'r': test_ir();          break;
    case 'f': print_sd();         break;
    case 'p': beep_once();       break;
    default: break;
  }
}

static void print_help() {
  Serial.println("[KEYS] ?=help  s=sys  d=drivers/caps  b=battery  c=charger  i=i2c  m=record/play  p=beep  r=ir  f=sd");
  Serial.println("[INFO] Same source for all boards; change only `pio run -e <env>`.");
}

static void serial_boot_banner(const char* phase) {
  Serial.printf("[BOOT] %s\n", phase ? phase : "?");
  Serial.flush();
}

void setup() {
  /* Start the debug console before touching SIC/buses/drivers.
   * On ESP32-S3/Cardputer this is normally native USB CDC, enabled by
   * platformio.ini. If early init ever stalls, the monitor still proves boot.
   */
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0) < 3000) delay(10);
  delay(50);

  Serial.println();
  serial_boot_banner("SIC Universal-Demo starting");

  const sic_board_t* board = sic_board_default();
  Serial.printf("[BOOT] selected board=%s\n", board ? board->name : "generic");
  Serial.flush();
  sic_begin_opts_t opts;
  opts.init_buses = 1;
  opts.lazy_drivers = 0;

  serial_boot_banner("calling sic_begin");
  int rc = sic_begin(board, &opts);
  Serial.printf("[SIC] board=%s sic_begin rc=%d\n", board ? board->name : "generic", rc);
  Serial.flush();

  print_help();
  print_sys_info();
  print_caps();
}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (isprint((unsigned char)c)) Serial.printf("[SER] '%c'\n", c);
    else                          Serial.printf("[SER] 0x%02X\n", (unsigned char)c);
    handle_command(c);
  }

  poll_keyboard();
  poll_encoder();
  sic_delay_ms(10);
}
