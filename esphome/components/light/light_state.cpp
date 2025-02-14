#include "light_state.h"
#include "esphome/core/log.h"

namespace esphome {
namespace light {

static const char *TAG = "light";

void LightState::start_transition_(const LightColorValues &target, uint32_t length) {
  this->transformer_ = make_unique<LightTransitionTransformer>(millis(), length, this->current_values, target);
  this->remote_values = this->transformer_->get_remote_values();
}

void LightState::start_flash_(const LightColorValues &target, uint32_t length) {
  LightColorValues end_colors = this->current_values;
  // If starting a flash if one is already happening, set end values to end values of current flash
  // Hacky but works
  if (this->transformer_ != nullptr)
    end_colors = this->transformer_->get_end_values();
  this->transformer_ = make_unique<LightFlashTransformer>(millis(), length, end_colors, target);
  this->remote_values = this->transformer_->get_remote_values();
}

LightState::LightState(const std::string &name, LightOutput *output) : Nameable(name), output_(output) {}

void LightState::set_immediately_(const LightColorValues &target, bool set_remote_values) {
  this->transformer_ = nullptr;
  this->current_values = target;
  if (set_remote_values) {
    this->remote_values = target;
  }
  this->next_write_ = true;
}

LightColorValues LightState::get_current_values() { return this->current_values; }

void LightState::publish_state() {
  this->remote_values_callback_.call();
  this->next_write_ = true;
}

LightColorValues LightState::get_remote_values() { return this->remote_values; }

std::string LightState::get_effect_name() {
  if (this->active_effect_index_ > 0)
    return this->effects_[this->active_effect_index_ - 1]->get_name();
  else
    return "None";
}

void LightState::start_effect_(uint32_t effect_index) {
  this->stop_effect_();
  if (effect_index == 0)
    return;

  this->active_effect_index_ = effect_index;
  auto *effect = this->get_active_effect_();
  effect->start_internal();
}

bool LightState::supports_effects() { return !this->effects_.empty(); }
void LightState::set_transformer_(std::unique_ptr<LightTransformer> transformer) {
  this->transformer_ = std::move(transformer);
}
void LightState::stop_effect_() {
  auto *effect = this->get_active_effect_();
  if (effect != nullptr) {
    effect->stop();
  }
  this->active_effect_index_ = 0;
}

void LightState::set_default_transition_length(uint32_t default_transition_length) {
  this->default_transition_length_ = default_transition_length;
}
#ifdef USE_JSON
void LightState::dump_json(JsonObject &root) {
  if (this->supports_effects())
    root["effect"] = this->get_effect_name();
  this->remote_values.dump_json(root, this->output_->get_traits());
}
#endif

void LightState::setup() {
  ESP_LOGCONFIG(TAG, "Setting up light '%s'...", this->get_name().c_str());

  this->output_->setup_state(this);
  for (auto *effect : this->effects_) {
    effect->init_internal(this);
  }

  auto call = this->make_call();
  LightStateRTCState recovered{};
  switch (this->restore_mode_) {
    case LIGHT_RESTORE_DEFAULT_OFF:
    case LIGHT_RESTORE_DEFAULT_ON:
      this->rtc_ = global_preferences.make_preference<LightStateRTCState>(this->get_object_id_hash());
      // Attempt to load from preferences, else fall back to default values from struct
      if (!this->rtc_.load(&recovered)) {
        recovered.state = this->restore_mode_ == LIGHT_RESTORE_DEFAULT_ON;
      }
      break;
    case LIGHT_ALWAYS_OFF:
      recovered.state = false;
      break;
    case LIGHT_ALWAYS_ON:
      recovered.state = true;
      break;
  }

  call.set_state(recovered.state);
  call.set_brightness_if_supported(recovered.brightness);
  call.set_red_if_supported(recovered.red);
  call.set_green_if_supported(recovered.green);
  call.set_blue_if_supported(recovered.blue);
  call.set_white_if_supported(recovered.white);
  call.set_color_temperature_if_supported(recovered.color_temp);
  if (recovered.effect != 0) {
    call.set_effect(recovered.effect);
  } else {
    call.set_transition_length_if_supported(0);
  }
  call.perform();
}
void LightState::loop() {
  // Apply effect (if any)
  auto *effect = this->get_active_effect_();
  if (effect != nullptr) {
    effect->apply();
  }

  // Apply transformer (if any)
  if (this->transformer_ != nullptr) {
    if (this->transformer_->is_finished()) {
      this->remote_values = this->current_values = this->transformer_->get_end_values();
      this->target_state_reached_callback_.call();
      if (this->transformer_->publish_at_end())
        this->publish_state();
      this->transformer_ = nullptr;
    } else {
      this->current_values = this->transformer_->get_values();
      this->remote_values = this->transformer_->get_remote_values();
    }
    this->next_write_ = true;
  }

  if (this->next_write_) {
    this->output_->write_state(this);
    this->next_write_ = false;
  }
}
LightTraits LightState::get_traits() { return this->output_->get_traits(); }
const std::vector<LightEffect *> &LightState::get_effects() const { return this->effects_; }
void LightState::add_effects(const std::vector<LightEffect *> effects) {
  this->effects_.reserve(this->effects_.size() + effects.size());
  for (auto *effect : effects) {
    this->effects_.push_back(effect);
  }
}
LightCall LightState::turn_on() { return this->make_call().set_state(true); }
LightCall LightState::turn_off() { return this->make_call().set_state(false); }
LightCall LightState::toggle() { return this->make_call().set_state(!this->remote_values.is_on()); }
LightCall LightState::make_call() { return LightCall(this); }
uint32_t LightState::hash_base() { return 1114400283; }
void LightState::dump_config() {
  ESP_LOGCONFIG(TAG, "Light '%s'", this->get_name().c_str());
  if (this->get_traits().get_supports_brightness()) {
    ESP_LOGCONFIG(TAG, "  Default Transition Length: %.1fs", this->default_transition_length_ / 1e3f);
    ESP_LOGCONFIG(TAG, "  Gamma Correct: %.2f", this->gamma_correct_);
  }
  if (this->get_traits().get_supports_color_temperature()) {
    ESP_LOGCONFIG(TAG, "  Min Mireds: %.1f", this->get_traits().get_min_mireds());
    ESP_LOGCONFIG(TAG, "  Max Mireds: %.1f", this->get_traits().get_max_mireds());
  }
}
#ifdef USE_MQTT_LIGHT
MQTTJSONLightComponent *LightState::get_mqtt() const { return this->mqtt_; }
void LightState::set_mqtt(MQTTJSONLightComponent *mqtt) { this->mqtt_ = mqtt; }
#endif

float LightState::get_setup_priority() const { return setup_priority::HARDWARE - 1.0f; }
LightOutput *LightState::get_output() const { return this->output_; }
void LightState::set_gamma_correct(float gamma_correct) { this->gamma_correct_ = gamma_correct; }
void LightState::current_values_as_binary(bool *binary) { this->current_values.as_binary(binary); }
void LightState::current_values_as_brightness(float *brightness) {
  this->current_values.as_brightness(brightness, this->gamma_correct_);
}
void LightState::current_values_as_rgb(float *red, float *green, float *blue, bool color_interlock) {
  auto traits = this->get_traits();
  this->current_values.as_rgb(red, green, blue, this->gamma_correct_, traits.get_supports_color_interlock());
}
void LightState::current_values_as_rgbw(float *red, float *green, float *blue, float *white, bool color_interlock) {
  auto traits = this->get_traits();
  this->current_values.as_rgbw(red, green, blue, white, this->gamma_correct_, traits.get_supports_color_interlock());
}
void LightState::current_values_as_rgbww(float *red, float *green, float *blue, float *cold_white, float *warm_white,
                                         bool constant_brightness, bool color_interlock) {
  auto traits = this->get_traits();
  this->current_values.as_rgbww(traits.get_min_mireds(), traits.get_max_mireds(), red, green, blue, cold_white,
                                warm_white, this->gamma_correct_, constant_brightness,
                                traits.get_supports_color_interlock());
}
void LightState::current_values_as_cwww(float *cold_white, float *warm_white, bool constant_brightness) {
  auto traits = this->get_traits();
  this->current_values.as_cwww(traits.get_min_mireds(), traits.get_max_mireds(), cold_white, warm_white,
                               this->gamma_correct_, constant_brightness);
}
void LightState::add_new_remote_values_callback(std::function<void()> &&send_callback) {
  this->remote_values_callback_.add(std::move(send_callback));
}
void LightState::add_new_target_state_reached_callback(std::function<void()> &&send_callback) {
  this->target_state_reached_callback_.add(std::move(send_callback));
}

LightEffect *LightState::get_active_effect_() {
  if (this->active_effect_index_ == 0)
    return nullptr;
  else
    return this->effects_[this->active_effect_index_ - 1];
}

}  // namespace light
}  // namespace esphome
