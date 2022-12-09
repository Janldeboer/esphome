#include "esp_color_correction.h"
#include "light_color_values.h"
#include "esphome/core/log.h"

namespace esphome {
namespace light {

void ESPColorCorrection::calculate_gamma_table(float gamma) {
  this->gamma_table_[0] = 0;
  for (uint16_t i = 1; i < 256; i++) {
    // corrected = val ^ gamma
    auto corrected = to_uint8_scale(gamma_correct(i / 255.0f, gamma));
    this->gamma_table_[i] = corrected;
  }
  if (gamma == 0.0f) {
    for (uint16_t i = 0; i < 256; i++)
      this->gamma_reverse_table_[i] = i;
    return;
  }
  this->gamma_reverse_table_[0] = 0;
  for (uint16_t i = 1; i < 256; i++) {
    // val = corrected ^ (1/gamma)
    auto uncorrected = to_uint8_scale(gamma_uncorrect(i / 255.0f, gamma));
    this->gamma_reverse_table_[i] = uncorrected;
  }
}

void ESPColorCorrection::calculate_brightness_table(float min_brightness, float max_brightness) {
  for (uint16_t i = 0; i < 256; i++) {
    auto corrected = to_uint8_scale(brightness_correct(i / 255.0f, min_brightness, max_brightness));
    this->brightness_table_[i] = corrected;
  }
  for (uint16_t i = 0; i < 256; i++) {
    auto uncorrected = to_uint8_scale(brightness_uncorrect(i / 255.0f, min_brightness, max_brightness));
    this->brightness_reverse_table_[i] = uncorrected;
  }
}

void ESPColorCorrection::calculate_correction_table(float gamma, float min_brightness, float max_brightness) {
  this->calculate_gamma_table(gamma);
  this->calculate_brightness_table(min_brightness, max_brightness);
  for (uint16_t i = 0; i < 256; i++) {
    this->correction_table_[i] = this->gamma_table_[this->brightness_table_[i]];
    this->correction_reverse_table_[i] = this->gamma_reverse_table_[this->brightness_reverse_table_[i]];
  }
}


}  // namespace light
}  // namespace esphome
