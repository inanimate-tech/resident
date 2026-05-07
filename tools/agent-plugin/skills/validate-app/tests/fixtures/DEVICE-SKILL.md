# Test Fixture Device

Minimal DEVICE-SKILL.md for validate-app tests. References two modules:
`screen` and `imu`.

## Hardware

A pretend screen and a pretend IMU.

## Lua Modules

### screen.*

```lua
screen.clear()
screen.flip()
local w = screen.width()
```

### imu.*

```lua
local ax, ay, az = imu.accel()
```
