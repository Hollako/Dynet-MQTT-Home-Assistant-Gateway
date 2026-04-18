#include "Globals.h"
#include <stdarg.h>

static String logBuffer;
static uint32_t logSeq = 0;

static Print* s_logSerial = nullptr;

// --- Implementation ---
void log_put_raw(const char* s) {
  logBuffer += s;
  logSeq++;
  if (logBuffer.length() > 8192) {
    logBuffer.remove(0, logBuffer.length() - 4096); // trim to avoid overflow
  }
  if (s_logSerial) s_logSerial->print(s);
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
  s_logSerial = &Serial1;   // ESP8266 TX-only (GPIO2)
}

void logs_init(size_t /*capBytes*/) {
  // no-op if your logger doesn't need a heap buffer init
  // (If you use a heap ring, you can call your real allocator here.)
}
