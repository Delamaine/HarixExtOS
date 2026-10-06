#include <unity.h>
#include <string.h>

#include "kernel/iot/relay_logic.h"

using namespace harixos::iot;

void setUp(void) {}
void tearDown(void) {}

// --- relaySetLevel: change detection ---

static void test_set_level_no_change_returns_false(void) {
  RelayState r = {2, false};
  TEST_ASSERT_FALSE(relaySetLevel(r, false));
  TEST_ASSERT_FALSE(r.level);
}

static void test_set_level_false_to_true(void) {
  RelayState r = {2, false};
  TEST_ASSERT_TRUE(relaySetLevel(r, true));
  TEST_ASSERT_TRUE(r.level);
}

static void test_set_level_true_to_false(void) {
  RelayState r = {4, true};
  TEST_ASSERT_TRUE(relaySetLevel(r, false));
  TEST_ASSERT_FALSE(r.level);
}

// --- relayStatePayload: MQTT payload ---

static void test_state_payload_on(void) {
  char buf[8];
  relayStatePayload(true, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("on", buf);
}

static void test_state_payload_off(void) {
  char buf[8];
  relayStatePayload(false, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("off", buf);
}

static void test_state_payload_buffer_too_small_yields_empty(void) {
  char buf[2] = {'x', 'x'};
  relayStatePayload(true, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("", buf);
  relayStatePayload(false, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("", buf);
}

// --- relayPersistLine: persistence format ---

static void test_persist_line_format(void) {
  char buf[16];
  relayPersistLine(2, true, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("2 1", buf);
  relayPersistLine(13, false, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("13 0", buf);
}

static void test_persist_line_buffer_too_small_yields_empty(void) {
  char buf[3] = {'x', 'x', 'x'};
  relayPersistLine(13, true, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("", buf);
}

// --- relayParseLevel: input validation for `relay set <name> <level>` ---

static void test_parse_level_on(void) {
  bool level = false;
  TEST_ASSERT_TRUE(relayParseLevel("on", &level));
  TEST_ASSERT_TRUE(level);
}

static void test_parse_level_off(void) {
  bool level = true;
  TEST_ASSERT_TRUE(relayParseLevel("off", &level));
  TEST_ASSERT_FALSE(level);
}

static void test_parse_level_rejects_typo(void) {
  bool level = false;
  TEST_ASSERT_FALSE(relayParseLevel("o", &level));
  TEST_ASSERT_FALSE(relayParseLevel("ON", &level));
  TEST_ASSERT_FALSE(relayParseLevel("", &level));
  TEST_ASSERT_FALSE(relayParseLevel("maybe", &level));
  TEST_ASSERT_FALSE(relayParseLevel(nullptr, &level));
}

// --- relayValidName: names flow into MQTT topics, so charset matters ---

static void test_valid_name_accepts_simple(void) {
  TEST_ASSERT_TRUE(relayValidName("relay1"));
  TEST_ASSERT_TRUE(relayValidName("garage-door"));
  TEST_ASSERT_TRUE(relayValidName("Pump_A"));
  TEST_ASSERT_TRUE(relayValidName("x"));
}

static void test_valid_name_rejects_bad_charset(void) {
  TEST_ASSERT_FALSE(relayValidName(""));           // empty
  TEST_ASSERT_FALSE(relayValidName(nullptr));      // null
  TEST_ASSERT_FALSE(relayValidName("a b"));        // space
  TEST_ASSERT_FALSE(relayValidName("a/b"));        // topic separator
  TEST_ASSERT_FALSE(relayValidName("a+b"));        // unsafe in topics
  TEST_ASSERT_FALSE(relayValidName("na\u00efve")); // non-ascii
}

static void test_valid_name_rejects_too_long(void) {
  TEST_ASSERT_TRUE(relayValidName("123456789012345"));   // 15 OK
  TEST_ASSERT_FALSE(relayValidName("1234567890123456")); // 16 too long
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_set_level_no_change_returns_false);
  RUN_TEST(test_set_level_false_to_true);
  RUN_TEST(test_set_level_true_to_false);
  RUN_TEST(test_state_payload_on);
  RUN_TEST(test_state_payload_off);
  RUN_TEST(test_state_payload_buffer_too_small_yields_empty);
  RUN_TEST(test_persist_line_format);
  RUN_TEST(test_persist_line_buffer_too_small_yields_empty);
  RUN_TEST(test_parse_level_on);
  RUN_TEST(test_parse_level_off);
  RUN_TEST(test_parse_level_rejects_typo);
  RUN_TEST(test_valid_name_accepts_simple);
  RUN_TEST(test_valid_name_rejects_bad_charset);
  RUN_TEST(test_valid_name_rejects_too_long);
  return UNITY_END();
}
