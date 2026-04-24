#include "Globals.h"
#include <stdarg.h>

static String logBuffer;
static uint32_t logSeq = 0;

static Print* s_logSerial = nullptr;

// --- Implementation ---
void log_put_raw(const char* s) {
  if (cfg.log_web) {
    logBuffer += s;
    logSeq++;
    if (logBuffer.length() > 2048) {
      logBuffer.remove(0, logBuffer.length() - 1024);
    }
  }
  if (s_logSerial) s_logSerial->print(s);  // Serial always active regardless of flag
}

void logf(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  log_put_raw(buf);
}

void logln(const String& s) {
  log_put_raw((s + "\n").c_str());
}

void logs_clear() {
  logBuffer = "";
  logSeq = 0;
}

uint32_t logs_seq() {
  return logSeq;
}

uint32_t logs_serialize_since(uint32_t sinceSeq, String& out) {
  out = "{\"seq\":" + String(logSeq) + ",\"lines\":[";
  // Split buffer into lines
  int start = 0;
  bool first = true;
  while (true) {
    int idx = logBuffer.indexOf('\n', start);
    if (idx == -1) break;
    String line = logBuffer.substring(start, idx);
    if (!first) out += ",";
    first = false;
    out += "\"" + line + "\"";
    start = idx + 1;
  }
  out += "]}";
  return logSeq;
}

void logs_serial_ready() {
  // Serial  = UART0 → USB on Wemos D1 / NodeMCU (what the PC sees in the monitor)
  // Serial1 = UART1 TX-only on GPIO2 (hardware debug pin, not USB)
  // We want USB so the PC monitor shows all logs.
  s_logSerial = &Serial;
}

void logs_init(size_t /*capBytes*/) {
  // no-op if your logger doesn't need a heap buffer init
  // (If you use a heap ring, you can call your real allocator here.)
}
