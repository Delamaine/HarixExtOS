#ifndef HARIXOS_STRING_STREAM_H
#define HARIXOS_STRING_STREAM_H

#include <Arduino.h>

namespace harixos {

// Header-only Stream implementation that wraps a String.
// Used by the MQTT drain loop to capture ScriptEngine output.
class StringStream : public Stream {
 public:
  explicit StringStream(const String &text = String()) : content(text), readPos(0) {}

  int available() override {
    return (int)(content.length() - readPos);
  }

  int read() override {
    if (readPos >= content.length()) return -1;
    return content[readPos++];
  }

  int peek() override {
    if (readPos >= content.length()) return -1;
    return content[readPos];
  }

  size_t write(uint8_t byte) override {
    content += (char)byte;
    return 1;
  }

  void flush() override {}

  void seek(int p) { readPos = (p < 0) ? 0 : (unsigned int)p; }
  void rewind() { readPos = 0; }
  const String &text() const { return content; }

 private:
  String content;
  unsigned int readPos = 0;
};

}  // namespace harixos

#endif
