# Extensions for Test Device

Minimal extension surface for validate-app tests. References one extra
module: `wifi`. Used to test the `--ref` flag.

## Lua Modules

### wifi.*

```lua
local strength = wifi.signal_strength()
local connected = wifi.is_connected()
```

## Validation stubs

```lua
wifi = setmetatable({
  signal_strength = function() return 75 end,
  is_connected   = function() return true end,
}, { __index = function() return function() end end })
```
