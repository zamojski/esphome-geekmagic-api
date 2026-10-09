# ESPHome GeekMagic SmallTV-Ultra HTTP API Emulator

An ESPHome external component that emulates the HTTP API used by the stock GeekMagic SmallTV-Ultra firmware. It allows compatible Home Assistant integrations, scripts, and clients to upload and display JPEG images on an SPI TFT display driven by ESPHome.

The component is designed for low-memory ESP32 devices, including the ESP32-C2 used by the reference configuration. It keeps only the most recently uploaded image in RAM and renders it directly to the display as RGB565 blocks.

> This is a community implementation. It is not affiliated with GeekMagic.

## Features

- Emulates the essential GeekMagic HTTP API on a configurable port, defaulting to `80`.
- Serves device metadata through `/v.json` and `/app.json`.
- Accepts multipart JPEG uploads through `/doUpload`.
- Exposes a virtual image list through `/filelist`.
- Supports image selection, theme reporting, and backlight brightness control through `/set`.
- Supports `/space.json`, `/brt.json`, and `/.sys/brt.json` for compatibility with clients that query storage and brightness state.
- Decodes baseline JPEG images with Espressif `esp_new_jpeg` and streams RGB565 big-endian blocks directly to the target display.
- Avoids a full-screen framebuffer and a filesystem.
- Uses one shared 32 KiB RAM buffer for the multipart request and the retained JPEG.
- Works with `display.mipi_spi` and ST7789V-based 240×240 displays in the reference setup.

## Important limitations

- Only the latest uploaded JPEG is retained; there is no persistent filesystem.
- `/filelist` is virtual and reports the name of that single retained image.
- JPEG uploads, including multipart overhead, must fit in 32 KiB by default.
- Input must be a **baseline JPEG**. Progressive JPEG files are not supported by `esp_new_jpeg`.
- The JPEG must not be larger than the configured display dimensions. The reference configuration uses 240×240 pixels.
- This component is intentionally optimized for one HTTP client and one upload at a time.

### Home Assistant compatibility

This component works with the [GeekMagic HACS](https://github.com/adrienbrault/geekmagic-hacs) integration in Home Assistant, supporting JPEG uploads, image display, and backlight brightness control through the emulated SmallTV-Ultra HTTP API.

## Installation from GitHub

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/zamojski/esphome-geekmagic-api.git
      ref: v1.0.0
    components: [geekmagic_api]
```

The GitHub shorthand is also supported:

```yaml
external_components:
  - source: github://zamojski/esphome-geekmagic-api@v1.0.0
    components: [geekmagic_api]
```

## Example configuration

```yaml
substitutions:
  name: esp-geekmagic-display
  friendly_name: GeekMagic Display

esphome:
  name: ${name}
  friendly_name: ${friendly_name}

esp32:
  board: esp32-c2-devkitm-1
  variant: esp32c2
  flash_size: 4MB
  framework:
    type: esp-idf
    sdkconfig_options:
      CONFIG_XTAL_FREQ_26: y
    components:
      - name: espressif/esp_new_jpeg
        ref: "1.0.2"

external_components:
  - source: github://zamojski/esphome-geekmagic-api@v1.0.0
    components: [geekmagic_api]

# # Enable Web
# web_server: # port conflict with httpd
#   port: 80
#   version: 3

# Enable logging
logger:
  level: INFO # save memory
  baud_rate: 74880 # the only way to obtain log, 0 disables logger

# api: # save memory
#   encryption:
#     key: SECRET
#   reboot_timeout: 0s

ota:
  - platform: esphome
    password: !secret esp_ota_password
# - platform: web_server # port conflict with httpd

safe_mode:
  disabled: false

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  min_auth_mode: WPA2
  reboot_timeout: 4min
  power_save_mode: NONE
  use_address: 192.168.1.100 # use static IP address
  # ap: # save memory
  #   ssid: $name
  #   password: !secret esp_ap_password

# captive_portal: # port conflict with httpd due to web_server being added

# save memory
mdns:
  disabled: true

geekmagic_api:
  id: geekmagic_http
  display_id: display_screen
  backlight_id: backlight
  port: 80
  model: "SmallTV-Ultra"
  version: "Ultra-V9.0.33"

spi:
  clk_pin: GPIO4
  mosi_pin: GPIO6

output:
  - platform: ledc
    id: backlight_pwm
    pin:
      number: GPIO18
      inverted: true

light:
  - platform: monochromatic
    id: backlight
    output: backlight_pwm
    name: Backlight
    restore_mode: ALWAYS_ON

display:
  - platform: mipi_spi
    id: display_screen
    model: ST7789V
    spi_mode: MODE3
    dc_pin: GPIO5
    reset_pin: GPIO1
    invert_colors: true
    dimensions:
      width: 240
      height: 240
    update_interval: never
    auto_clear_enabled: false
    show_test_card: false

# status_led: # save memory
#   pin: GPIO8
```

Do not enable ESPHome `web_server` on port `80`; it conflicts with this component's HTTP server. ESPHome Native API and ESPHome OTA use different ports and may be enabled separately if memory permits.

## Supported HTTP API

| Endpoint | Method | Purpose |
|---|---:|---|
| `/v.json` | GET | Device model, firmware version, theme, and brightness |
| `/app.json` | GET | Alias of `/v.json` |
| `/space.json` | GET | Virtual storage capacity response |
| `/brt.json` | GET | Current brightness |
| `/.sys/brt.json` | GET | Alias of `/brt.json` |
| `/filelist` | GET | Virtual list containing the latest uploaded image |
| `/doUpload` | POST | Multipart JPEG upload |
| `/set?theme=N` | GET | Set the reported theme number |
| `/set?img=/image/name.jpg` | GET | Render the retained JPEG |
| `/set?brt=0..100` | GET | Set backlight brightness |

A successful upload stores the decoded source JPEG in RAM, assigns the image theme (`3`), and returns `OK`. The image is rendered on the ESPHome main loop instead of the HTTP server task.

## Design notes

### Direct display rendering

The component calls `draw_pixels_at()` for each decoded JPEG block. It must **not** call `display->update()` after rendering. With `mipi_spi` and no display lambda or pages, an explicit update can render ESPHome's test card and overwrite the image.

### Color format

The JPEG decoder outputs `JPEG_PIXEL_FORMAT_RGB565_BE`, and the display call specifies `COLOR_BITNESS_565` with `big_endian=true`. This matches the reference `mipi_spi` configuration, which uses 16-bit big-endian pixels.

### Memory model

The component allocates one 32 KiB buffer during setup. That buffer first receives the multipart body; once the JPEG start and end markers are located, the JPEG is moved to the beginning of the same buffer. JPEG decoder output is allocated only for the duration of rendering.

### Server limits

The HTTP server is deliberately constrained for small devices:

- One open socket.
- 4096-byte server task stack.
- A 2048-byte request-header limit.
- A 128-byte URI limit.
- One concurrent upload/render operation.

Increase these values only after measuring free heap and the largest contiguous 8-bit heap block on the target board.

## Development and testing

1. Validate and compile an example configuration with the target ESPHome version.
2. Verify `GET /v.json`, `GET /space.json`, and `GET /brt.json` return HTTP 200.
3. Upload a small baseline JPEG using multipart form data.
4. Call `/set?img=/image/<filename>` and verify that the JPEG remains visible.
5. Test brightness values `0`, `1`, `50`, and `100`.
6. Test an oversized upload and a progressive JPEG; both should fail cleanly.

Example upload:

```bash
curl -i \
  -F "file=@test-240.jpg;type=image/jpeg" \
  "http://DEVICE_IP/doUpload?dir=/image/"

curl -i "http://DEVICE_IP/set?theme=3"
curl -i "http://DEVICE_IP/set?img=/image/test-240.jpg"
```

## License

This project is licensed under the **GNU General Public License v3.0 only** (`GPL-3.0-only`).

This component is intended to be compiled and linked into ESPHome firmware. ESPHome’s C++ code is distributed under GPLv3, so GPL-3.0-only is a clear and compatible licensing choice for this reusable external component.

## Disclaimer

Use this component at your own risk. It controls a display and allocates memory on a constrained embedded device. Verify behavior on non-critical hardware before deploying it in an unattended environment.
