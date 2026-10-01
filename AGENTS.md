# AGENTS.md

## Flash Procedure

- **Board**: NodeMCU v2 (ESP8266EX, 4MB Flash)
- **Environment**: `nodemcuv2` (NOT `esp01_1m` — that is for a different board)
- **Port**: COM3
- **Baud**: 115200 (PlatformIO default)

### Build and flash

```
python -m platformio run -e nodemcuv2 --target upload
```

### Troubleshooting

- If `esptool` says "Invalid head of packet (0x08)", you are building for the wrong environment (`esp01_1m`). Use `-e nodemcuv2`.
- If `COM3: Access is denied`, close the serial monitor first.
- Board has an auto-reset circuit — no manual button press needed.
