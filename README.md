# FLOATY - ESP32-C3 Flap Servo Controller

A dual-servo flapping controller for floating robots. Built for the ESP32-C3 Super Mini with HW-566 expansion board, driving two GH-S37D micro servos with smooth, synchronized motion.

## Hardware

| Component | Role |
|-----------|------|
| ESP32-C3 Super Mini | Main microcontroller |
| HW-566 expansion board | 3-pin servo headers, 5V power rail |
| 2x GH-S37D micro servos | 9g analog servos (500-2500 us pulse) |

### Wiring

```
HW-566 Servo Headers (S / V / G):
  Servo 1 signal -> GPIO4
  Servo 2 signal -> GPIO5
  V  -> 5V rail on HW-566 (NOT the C3's 3.3V pin)
  G  -> GND, shared with ESP32-C3
```

## Quick Start

1. **Build & Flash**: Open in PlatformIO, select `esp32-c3-devkitm-1` environment, build and upload.

2. **Connect**: The board creates its own WiFi network `FLOATY-SERVO` (password: `servotest123`).

3. **Access Web Console**:
   - Open `http://192.168.4.1` or `http://floaty.local` in a browser
   - Control servos via sliders and buttons

4. **Serial Control**: Open serial monitor at 115200 baud, type `h` for command list.

## Usage

### Web Console

Point your browser to either:
- `http://192.168.4.1` (direct IP)
- `http://floaty.local` (via mDNS)

The web interface provides:
- Real-time sliders for each servo
- Range configuration (from/to angles)
- Motion mode selection (sweep, endpoints, center, idle)
- Stroke timing adjustment
- Servo attach/detach controls

### Serial Commands

Connect via USB (115200 baud). Type `h` for help, or see:

| Command | Description |
|---------|-------------|
| `s` | Start sweep mode (continuous motion through ranges) |
| `e` | Start endpoints mode (step between range ends, 1s each) |
| `c` | Center both servos at 90 degrees |
| `x` | Stop motion, hold current position |
| `d` | Detach servos (go limp, no PWM) |
| `a` | Re-attach servos, park at range middle |
| `1<ang>` | Set servo 1 angle (e.g., `145`) |
| `2<ang>` | Set servo 2 angle (e.g., `20`) |
| `b<ang>` | Set both servos to angle (e.g., `b90`) |
| `r<i> <f> <t>` | Set servo range (e.g., `r1 30 150`) |
| `+` | Decrease stroke time by 50ms |
| `-` | Increase stroke time by 50ms |
| `p` | Print current state |
| `w` | Print WiFi/AP status |
| `h` | Show this help |

### Range Configuration

Each servo has an independent range (from/to angles). When using sweep or endpoints mode:
- Both servos move in sync based on a shared phase
- Servo 2 defaults to mirrored range (180->0) for opposite-side linkages
- Ranges are saved to flash after 1.5 seconds of inactivity

## Configuration

Edit `platformio.ini` for build options, or modify constants in `src/main.cpp`:

| Constant | Default | Description |
|----------|---------|-------------|
| `SERVO1_PIN` | 4 | GPIO for servo 1 signal |
| `SERVO2_PIN` | 5 | GPIO for servo 2 signal |
| `LED_PIN` | 8 | Onboard LED (active low) |
| `PULSE_MIN_US` | 500 | Minimum servo pulse width (us) |
| `PULSE_MAX_US` | 2500 | Maximum servo pulse width (us) |
| `SERVO_HZ` | 50 | Servo update frequency |
| `STROKE_MS_DEFAULT` | 1000 | Default stroke time (ms) |
| `AP_SSID` | FLOATY-SERVO | Access point SSID |
| `AP_PASS` | servotest123 | Access point password |
| `HOSTNAME` | floaty | mDNS hostname |

## Dependencies

- [PlatformIO](https://platformio.org/) - Build system
- [ESP32Servo](https://github.com/madhephaestus/ESP32Servo) - Servo library for ESP32
- [U8g2](https://github.com/olikraus/U8g2) - Graphics library (included for potential display support)
- Arduino framework

## WiFi Settings

The board operates as a standalone access point:
- **SSID**: FLOATY-SERVO
- **Password**: servotest123
- **IP Address**: 192.168.4.1
- **Channel**: 1 (configurable)
- **Max Clients**: 4
- **Tx Power**: 8.5 dBm (reduced for stability on C3's antenna)

These can be changed in `src/main.cpp` by modifying the `AP_*` constants.

## Motion Details

### Sweep Mode
- Uses cosine profile for smooth acceleration/deceleration
- Shared phase ensures both servos reach endpoints simultaneously
- Stroke time = time for one direction (from->to or to->from)
- Full flap cycle = 2 * stroke time

### Endpoints Mode
- Servos step between range endpoints
- 1-second hold at each endpoint
- Useful for testing linkage limits

### Center Mode
- Both servos hold at 90 degrees
- Simple static position

## Tips

- **Servo Buzzing**: If servos buzz or slam at endpoints, narrow the pulse range to 600-2400 us
- **Short Range**: Reduce WiFi tx power further if AP doesn't appear reliably
- **Calibration**: Use `r` command to set ranges that match your linkage's physical limits
- **Preview**: When adjusting ranges via web, use the preview parameter to move servo to the end being edited

## License

MIT License - feel free to use and modify for your own projects.