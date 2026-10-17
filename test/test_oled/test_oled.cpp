#include <unity.h>

#include <cstring>

#include "api/oled_font.h"

using namespace harixos::api;

void setUp(void) {}
void tearDown(void) {}

static void test_glyph_spot_values(void) {
  // Classic 5x7 table anchors (chars 0x20..0x7E, 5 bytes each).
  const uint8_t* g0 = oledGlyph('0');
  TEST_ASSERT_EQUAL_HEX8(0x3E, oledGlyphByte(g0, 0));
  TEST_ASSERT_EQUAL_HEX8(0x51, oledGlyphByte(g0, 1));
  TEST_ASSERT_EQUAL_HEX8(0x49, oledGlyphByte(g0, 2));
  TEST_ASSERT_EQUAL_HEX8(0x45, oledGlyphByte(g0, 3));
  TEST_ASSERT_EQUAL_HEX8(0x3E, oledGlyphByte(g0, 4));
  TEST_ASSERT_EQUAL_HEX8(0x7F, oledGlyphByte(oledGlyph('H'), 0));
  // Control bytes map to '?' rather than a random glyph.
  TEST_ASSERT_EQUAL_HEX8(oledGlyphByte(oledGlyph('?'), 0),
                         oledGlyphByte(oledGlyph('\x01'), 0));
  TEST_ASSERT_EQUAL_HEX8(0x02, oledGlyphByte(oledGlyph('\x01'), 0));
}

static void test_pack_line_pads_to_blank(void) {
  uint8_t out[OLED_LINE_BYTES];
  oledPackLine("Hi", out);
  // Slot 0 'H', slot 1 'i', then 19 blank slots of zeros.
  TEST_ASSERT_EQUAL_HEX8(0x7F, oledGlyphByte(out, 0));  // 'H' col0
  TEST_ASSERT_EQUAL_HEX8(0x00, out[5]);                 // 1px gap after glyph
  TEST_ASSERT_EQUAL_HEX8(0x00, oledGlyphByte(out, 6));  // 'i' col0
  TEST_ASSERT_EQUAL_HEX8(0x44, oledGlyphByte(out, 7));  // 'i' col1
  for (int i = 12; i < OLED_LINE_BYTES; ++i) {
    TEST_ASSERT_EQUAL_HEX8(0x00, out[i]);
  }
}

static void test_pack_line_truncates_at_21(void) {
  uint8_t out[OLED_LINE_BYTES];
  const char* longText = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";  // 30 chars
  oledPackLine(longText, out);
  // 21 slots only; the 22nd char must not appear.
  const uint8_t* a = oledGlyph('A');
  for (int slot = 0; slot < OLED_LINE_CHARS; ++slot) {
    TEST_ASSERT_EQUAL_HEX8(oledGlyphByte(a, 0), oledGlyphByte(out, slot * 6));
  }
  TEST_ASSERT_EQUAL_INT(OLED_LINE_BYTES, 21 * 6);
}

static void test_pack_empty_line_is_all_blank(void) {
  uint8_t out[OLED_LINE_BYTES];
  memset(out, 0xAA, sizeof(out));
  oledPackLine("", out);
  for (int i = 0; i < OLED_LINE_BYTES; ++i) {
    TEST_ASSERT_EQUAL_HEX8(0x00, out[i]);
  }
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_glyph_spot_values);
  RUN_TEST(test_pack_line_pads_to_blank);
  RUN_TEST(test_pack_line_truncates_at_21);
  RUN_TEST(test_pack_empty_line_is_all_blank);
  return UNITY_END();
}
