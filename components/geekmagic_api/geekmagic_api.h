/*
 * SPDX-FileCopyrightText: 2026 Adam Zamojski
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <string>

#include "esphome/components/display/display.h"
#include "esphome/components/light/light_state.h"
#include "esphome/core/component.h"

#include "esp_http_server.h"

namespace esphome {
namespace geekmagic_api {

class GeekMagicApi : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_display(display::Display *display) { this->display_ = display; }
  void set_backlight(light::LightState *light) { this->backlight_ = light; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_model(const std::string &model) { this->model_ = model; }
  void set_version(const std::string &version) { this->version_ = version; }

  void handle_status(httpd_req_t *req);
  void handle_set(httpd_req_t *req);
  void handle_upload(httpd_req_t *req);
  void handle_filelist(httpd_req_t *req);

 protected:
  void start_server_();
  void draw_pending_();
  void apply_brightness_(int brightness);
  bool decode_and_draw_(const uint8_t *jpg, size_t len);
  bool extract_jpeg_(const uint8_t *body, size_t len, size_t *start, size_t *jpeg_len);
  void extract_filename_(const uint8_t *body, size_t len);

  display::Display *display_{nullptr};
  light::LightState *backlight_{nullptr};
  uint16_t port_{80};
  std::string model_{"SmallTV-Ultra"};
  std::string version_{"Ultra-V9.0.33"};
  httpd_handle_t server_{nullptr};

  uint8_t *jpeg_{nullptr};
  size_t jpeg_len_{0};
  bool pending_{false};
  int theme_{3};
  int brightness_{100};
  int pending_brightness_{-1};
  char filename_[48]{"ha.jpg"};
};

}  // namespace geekmagic_api
}  // namespace esphome
