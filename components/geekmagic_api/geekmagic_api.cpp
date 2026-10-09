/*
 * SPDX-FileCopyrightText: 2026 Adam Zamojski
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "geekmagic_api.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esphome/core/log.h"

#include "esp_err.h"
#include "esp_jpeg_dec.h"

namespace esphome {
namespace geekmagic_api {

static const char *const TAG = "geekmagic_api";
static constexpr size_t MAX_JPEG = 32 * 1024;
static constexpr size_t MAX_BODY = MAX_JPEG;

static esp_err_t status_handler(httpd_req_t *req) {
  static_cast<GeekMagicApi *>(req->user_ctx)->handle_status(req);
  return ESP_OK;
}

static esp_err_t set_handler(httpd_req_t *req) {
  static_cast<GeekMagicApi *>(req->user_ctx)->handle_set(req);
  return ESP_OK;
}

static esp_err_t upload_handler(httpd_req_t *req) {
  static_cast<GeekMagicApi *>(req->user_ctx)->handle_upload(req);
  return ESP_OK;
}

static esp_err_t filelist_handler(httpd_req_t *req) {
  static_cast<GeekMagicApi *>(req->user_ctx)->handle_filelist(req);
  return ESP_OK;
}

static esp_err_t fallback_get_handler(httpd_req_t *req) {
  ESP_LOGW(TAG, "Unsupported GET request: %s", req->uri);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not Found");
  return ESP_OK;
}

static void send_text(httpd_req_t *req, const char *type, const std::string &body) {
  httpd_resp_set_type(req, type);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, body.c_str(), body.size());
}

void GeekMagicApi::setup() {
  this->jpeg_ = static_cast<uint8_t *>(malloc(MAX_JPEG));
  if (this->jpeg_ == nullptr) {
    ESP_LOGE(TAG, "Unable to allocate the JPEG buffer (%u bytes)", static_cast<unsigned>(MAX_JPEG));
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "JPEG buffer allocated: %u bytes", static_cast<unsigned>(MAX_JPEG));
  this->start_server_();
}

void GeekMagicApi::loop() {
  if (this->pending_brightness_ >= 0) {
    const int brightness = this->pending_brightness_;
    this->pending_brightness_ = -1;
    this->apply_brightness_(brightness);
  }

  if (this->pending_) {
    this->draw_pending_();
  }
}

void GeekMagicApi::dump_config() {
  ESP_LOGCONFIG(TAG, "GeekMagic API:");
  ESP_LOGCONFIG(TAG, "  Model: %s", this->model_.c_str());
  ESP_LOGCONFIG(TAG, "  Version: %s", this->version_.c_str());
  ESP_LOGCONFIG(TAG, "  Port: %u", this->port_);
  ESP_LOGCONFIG(TAG, "  Theme: %d", this->theme_);
}

void GeekMagicApi::start_server_() {
  if (this->server_ != nullptr) {
    return;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = this->port_;
  config.ctrl_port = this->port_ + 1;
  config.max_uri_handlers = 10;
  config.max_open_sockets = 1;
  config.stack_size = 4096;
  config.max_req_hdr_len = 2048;
  config.max_uri_len = 128;
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.lru_purge_enable = true;
  config.recv_wait_timeout = 10;
  config.send_wait_timeout = 10;

  httpd_handle_t server = nullptr;
  const esp_err_t err = httpd_start(&server, &config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to start HTTP server on port %u: %s (%d)", this->port_, esp_err_to_name(err),
             static_cast<int>(err));
    return;
  }

  this->server_ = server;
  const httpd_uri_t routes[] = {
      {.uri = "/v.json", .method = HTTP_GET, .handler = status_handler, .user_ctx = this},
      {.uri = "/app.json", .method = HTTP_GET, .handler = status_handler, .user_ctx = this},
      {.uri = "/set", .method = HTTP_GET, .handler = set_handler, .user_ctx = this},
      {.uri = "/doUpload", .method = HTTP_POST, .handler = upload_handler, .user_ctx = this},
      {.uri = "/filelist", .method = HTTP_GET, .handler = filelist_handler, .user_ctx = this},
      {.uri = "/space.json", .method = HTTP_GET, .handler = status_handler, .user_ctx = this},
      {.uri = "/brt.json", .method = HTTP_GET, .handler = status_handler, .user_ctx = this},
      {.uri = "/.sys/brt.json", .method = HTTP_GET, .handler = status_handler, .user_ctx = this},
      {.uri = "/*", .method = HTTP_GET, .handler = fallback_get_handler, .user_ctx = this},
  };

  for (const auto &route : routes) {
    const esp_err_t route_err = httpd_register_uri_handler(server, &route);
    if (route_err != ESP_OK) {
      ESP_LOGE(TAG, "Unable to register route %s: %s", route.uri, esp_err_to_name(route_err));
    }
  }

  ESP_LOGI(TAG, "HTTP API started on port %u", this->port_);
}

void GeekMagicApi::handle_status(httpd_req_t *req) {
  if (strcmp(req->uri, "/space.json") == 0) {
    send_text(req, "application/json", "{\"total\":32768,\"free\":32768}");
    return;
  }

  if (strcmp(req->uri, "/brt.json") == 0 || strcmp(req->uri, "/.sys/brt.json") == 0) {
    char body[32];
    snprintf(body, sizeof(body), "{\"brt\":%d}", this->brightness_);
    send_text(req, "application/json", body);
    return;
  }

  char body[160];
  snprintf(body, sizeof(body), "{\"m\":\"%s\",\"v\":\"%s\",\"theme\":%d,\"brt\":%d}",
           this->model_.c_str(), this->version_.c_str(), this->theme_, this->brightness_);
  send_text(req, "application/json", body);
}

void GeekMagicApi::handle_set(httpd_req_t *req) {
  char query[256];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char value[96];

    if (httpd_query_key_value(query, "theme", value, sizeof(value)) == ESP_OK) {
      this->theme_ = atoi(value);
      ESP_LOGI(TAG, "Theme set to %d", this->theme_);
    }

    if (httpd_query_key_value(query, "img", value, sizeof(value)) == ESP_OK) {
      ESP_LOGI(TAG, "Image selected: %s", value);
      this->pending_ = this->jpeg_len_ > 0;
    }

    if (httpd_query_key_value(query, "brt", value, sizeof(value)) == ESP_OK) {
      this->brightness_ = atoi(value);
      if (this->brightness_ < 0) this->brightness_ = 0;
      if (this->brightness_ > 100) this->brightness_ = 100;
      this->pending_brightness_ = this->brightness_;
      ESP_LOGI(TAG, "Brightness set to %d", this->brightness_);
    }
  }

  send_text(req, "text/plain", "OK");
}

void GeekMagicApi::handle_filelist(httpd_req_t *req) {
  const std::string body = std::string("[\"/image/") + this->filename_ + "\"]";
  send_text(req, "application/json", body);
}

bool GeekMagicApi::extract_jpeg_(const uint8_t *body, size_t len, size_t *start, size_t *jpeg_len) {
  int soi = -1;
  int eoi = -1;

  for (size_t i = 0; i + 1 < len; i++) {
    if (body[i] == 0xFF && body[i + 1] == 0xD8 && soi < 0) {
      soi = static_cast<int>(i);
    }
    if (body[i] == 0xFF && body[i + 1] == 0xD9 && soi >= 0) {
      eoi = static_cast<int>(i + 2);
    }
  }

  if (soi < 0 || eoi <= soi) {
    return false;
  }

  *start = static_cast<size_t>(soi);
  *jpeg_len = static_cast<size_t>(eoi - soi);
  return *jpeg_len <= MAX_JPEG;
}

void GeekMagicApi::extract_filename_(const uint8_t *body, size_t len) {
  static constexpr char KEY[] = "filename=\"";
  const size_t key_len = sizeof(KEY) - 1;
  size_t start = len;

  for (size_t i = 0; i + key_len <= len; i++) {
    if (memcmp(body + i, KEY, key_len) == 0) {
      start = i + key_len;
      break;
    }
  }

  if (start == len) {
    strncpy(this->filename_, "ha.jpg", sizeof(this->filename_) - 1);
    this->filename_[sizeof(this->filename_) - 1] = '\0';
    return;
  }

  char filename[sizeof(this->filename_)]{};
  size_t written = 0;
  for (size_t i = start; i < len && body[i] != '\"'; i++) {
    const char c = static_cast<char>(body[i]);
    if (c == '/' || c == '\\') {
      written = 0;
      continue;
    }
    const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    if (safe && written < sizeof(filename) - 1) {
      filename[written++] = c;
    }
  }

  if (written == 0) {
    strncpy(this->filename_, "ha.jpg", sizeof(this->filename_) - 1);
  } else {
    strncpy(this->filename_, filename, sizeof(this->filename_) - 1);
  }
  this->filename_[sizeof(this->filename_) - 1] = '\0';
}

void GeekMagicApi::handle_upload(httpd_req_t *req) {
  ESP_LOGI(TAG, "Upload request received: %d bytes", req->content_len);

  if (req->content_len <= 0 || static_cast<size_t>(req->content_len) > MAX_BODY) {
    ESP_LOGW(TAG, "Upload rejected: %d bytes exceeds the %u-byte limit", req->content_len,
             static_cast<unsigned>(MAX_BODY));
    httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "JPEG too large");
    return;
  }

  if (this->pending_) {
    ESP_LOGW(TAG, "Upload rejected: the previous image is still pending");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Busy");
    return;
  }

  int received = 0;
  while (received < req->content_len) {
    const int result = httpd_req_recv(req, reinterpret_cast<char *>(this->jpeg_) + received,
                                     req->content_len - received);
    if (result == HTTPD_SOCK_ERR_TIMEOUT) {
      continue;
    }
    if (result <= 0) {
      ESP_LOGW(TAG, "Upload receive failed: %d", result);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
      return;
    }
    received += result;
  }

  size_t start = 0;
  size_t jpeg_len = 0;
  if (!this->extract_jpeg_(this->jpeg_, static_cast<size_t>(received), &start, &jpeg_len)) {
    ESP_LOGW(TAG, "Upload rejected: no valid JPEG was found");
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No JPEG found");
    return;
  }

  this->extract_filename_(this->jpeg_, static_cast<size_t>(received));
  if (start > 0) {
    memmove(this->jpeg_, this->jpeg_ + start, jpeg_len);
  }

  this->jpeg_len_ = jpeg_len;
  this->pending_ = true;
  this->theme_ = 3;
  ESP_LOGI(TAG, "Image accepted: %s (%u bytes)", this->filename_, static_cast<unsigned>(jpeg_len));
  send_text(req, "text/plain", "OK");
}

void GeekMagicApi::apply_brightness_(int brightness) {
  if (this->backlight_ == nullptr) {
    return;
  }

  if (brightness <= 0) {
    auto call = this->backlight_->turn_off();
    call.perform();
    return;
  }

  auto call = this->backlight_->turn_on();
  call.set_brightness(brightness / 100.0f);
  call.perform();
}

void GeekMagicApi::draw_pending_() {
  if (this->display_ == nullptr || this->jpeg_len_ == 0) {
    this->pending_ = false;
    return;
  }

  if (!this->decode_and_draw_(this->jpeg_, this->jpeg_len_)) {
    ESP_LOGE(TAG, "Unable to render the JPEG image");
  }
  this->pending_ = false;
}

bool GeekMagicApi::decode_and_draw_(const uint8_t *jpg, size_t len) {
  jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
  config.output_type = JPEG_PIXEL_FORMAT_RGB565_BE;
  config.rotate = JPEG_ROTATE_0D;
  config.block_enable = true;

  jpeg_dec_handle_t decoder = nullptr;
  if (jpeg_dec_open(&config, &decoder) != JPEG_ERR_OK) {
    ESP_LOGE(TAG, "Unable to open JPEG decoder");
    return false;
  }

  jpeg_dec_io_t io{};
  jpeg_dec_header_info_t info{};
  io.inbuf = const_cast<uint8_t *>(jpg);
  io.inbuf_len = len;

  if (jpeg_dec_parse_header(decoder, &io, &info) != JPEG_ERR_OK) {
    ESP_LOGE(TAG, "Unable to parse JPEG header");
    jpeg_dec_close(decoder);
    return false;
  }

  if (info.width > this->display_->get_width() || info.height > this->display_->get_height()) {
    ESP_LOGE(TAG, "JPEG dimensions %ux%u exceed display dimensions %dx%d", info.width, info.height,
             this->display_->get_width(), this->display_->get_height());
    jpeg_dec_close(decoder);
    return false;
  }

  int output_len = 0;
  int block_count = 0;
  jpeg_dec_get_outbuf_len(decoder, &output_len);
  jpeg_dec_get_process_count(decoder, &block_count);
  if (output_len <= 0 || block_count <= 0 || info.width == 0) {
    ESP_LOGE(TAG, "Invalid JPEG header: %ux%u", info.width, info.height);
    jpeg_dec_close(decoder);
    return false;
  }

  auto *block = static_cast<uint8_t *>(malloc(output_len));
  if (block == nullptr) {
    ESP_LOGE(TAG, "Unable to allocate JPEG output block (%d bytes)", output_len);
    jpeg_dec_close(decoder);
    return false;
  }

  io.outbuf = block;
  const int rows_per_block = output_len / (info.width * 2);
  int y = 0;
  bool ok = rows_per_block > 0;

  for (int i = 0; ok && i < block_count; i++) {
    if (jpeg_dec_process(decoder, &io) != JPEG_ERR_OK) {
      ESP_LOGE(TAG, "JPEG decode failed at block %d", i);
      ok = false;
      break;
    }

    int height = rows_per_block;
    if (y + height > info.height) {
      height = info.height - y;
    }

    if (height > 0) {
      this->display_->draw_pixels_at(0, y, info.width, height, block, display::COLOR_ORDER_RGB,
                                     display::COLOR_BITNESS_565, true);
      y += height;
    }
  }

  free(block);
  jpeg_dec_close(decoder);

  if (ok && y == info.height) {
    ESP_LOGI(TAG, "Image rendered: %ux%u in %d blocks", info.width, info.height, block_count);
    return true;
  }

  ESP_LOGE(TAG, "Incomplete JPEG render: %d of %u rows", y, info.height);
  return false;
}

}  // namespace geekmagic_api
}  // namespace esphome
