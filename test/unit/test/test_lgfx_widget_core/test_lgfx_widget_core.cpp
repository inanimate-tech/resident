// The pure half of the Lgfx widget (ResidentLgfxWidgetCore.h): what a Lua
// coordinate and colour mean, and the byte swap from LovyanGFX's sprite to
// LVGL.
#include <unity.h>
#include "ResidentLgfxWidgetCore.h"

#include <cmath>

using namespace Resident;

void setUp() {}
void tearDown() {}

void test_coordinates_round_to_the_nearest_pixel() {
  TEST_ASSERT_EQUAL(3, lgfxw::px(3.0));
  TEST_ASSERT_EQUAL(3, lgfxw::px(2.5));      // half rounds up
  TEST_ASSERT_EQUAL(2, lgfxw::px(2.49));
  TEST_ASSERT_EQUAL(-2, lgfxw::px(-2.5));    // floor(x + 0.5): up, not away from zero
  TEST_ASSERT_EQUAL(-3, lgfxw::px(-2.51));
}

void test_far_and_bad_coordinates_clamp_rather_than_wrap() {
  TEST_ASSERT_EQUAL(1000000, lgfxw::px(1e12));
  TEST_ASSERT_EQUAL(-1000000, lgfxw::px(-1e12));
  TEST_ASSERT_EQUAL(0, lgfxw::px(std::nan("")));
}

void test_hex_colours_parse_long_and_short() {
  uint32_t v = 0;
  TEST_ASSERT_TRUE(lgfxw::parseHex("#c8a24a", v));
  TEST_ASSERT_EQUAL_HEX32(0xc8a24a, v);
  TEST_ASSERT_TRUE(lgfxw::parseHex("#FFF", v));
  TEST_ASSERT_EQUAL_HEX32(0xffffff, v);
  TEST_ASSERT_TRUE(lgfxw::parseHex("#08f", v));
  TEST_ASSERT_EQUAL_HEX32(0x0088ff, v);
}

void test_anything_else_is_not_a_colour() {
  uint32_t v = 0x123456;
  TEST_ASSERT_FALSE(lgfxw::parseHex("c8a24a", v));
  TEST_ASSERT_FALSE(lgfxw::parseHex("#c8a2", v));
  TEST_ASSERT_FALSE(lgfxw::parseHex("#c8a24g", v));
  TEST_ASSERT_FALSE(lgfxw::parseHex("", v));
  TEST_ASSERT_FALSE(lgfxw::parseHex(nullptr, v));
  TEST_ASSERT_EQUAL_HEX32(0x123456, v);   // untouched on failure
}

void test_sprite_pixels_are_byte_swapped_into_lvgl_order() {
  const uint16_t sprite[3] = {0x00F8, 0xE007, 0x1F00};   // red, green, blue, wire order
  uint16_t lv[3] = {};
  lgfxw::copySwapped(sprite, lv, 3);
  TEST_ASSERT_EQUAL_HEX16(0xF800, lv[0]);
  TEST_ASSERT_EQUAL_HEX16(0x07E0, lv[1]);
  TEST_ASSERT_EQUAL_HEX16(0x001F, lv[2]);
}

void test_colours_pack_to_rgb565() {
  TEST_ASSERT_EQUAL_HEX16(0xF800, lgfxw::rgb565(0xff0000));
  TEST_ASSERT_EQUAL_HEX16(0x07E0, lgfxw::rgb565(0x00ff00));
  TEST_ASSERT_EQUAL_HEX16(0x001F, lgfxw::rgb565(0x0000ff));
  TEST_ASSERT_EQUAL_HEX16(0xCD09, lgfxw::rgb565(0xc8a24a));   // a gold
}

void test_the_transparent_colour_gets_alpha_zero() {
  const uint16_t sprite[3] = {0x0000, 0x00F8, 0x0000};   // black, red, black
  uint16_t lv[3] = {};
  uint8_t a[3] = {9, 9, 9};
  lgfxw::copySwappedKeyed(sprite, lv, a, 3, lgfxw::rgb565(0x000000));
  TEST_ASSERT_EQUAL_HEX16(0xF800, lv[1]);
  TEST_ASSERT_EQUAL(0, a[0]);
  TEST_ASSERT_EQUAL(255, a[1]);
  TEST_ASSERT_EQUAL(0, a[2]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_coordinates_round_to_the_nearest_pixel);
  RUN_TEST(test_far_and_bad_coordinates_clamp_rather_than_wrap);
  RUN_TEST(test_hex_colours_parse_long_and_short);
  RUN_TEST(test_anything_else_is_not_a_colour);
  RUN_TEST(test_sprite_pixels_are_byte_swapped_into_lvgl_order);
  RUN_TEST(test_colours_pack_to_rgb565);
  RUN_TEST(test_the_transparent_colour_gets_alpha_zero);
  return UNITY_END();
}
