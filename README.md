# Hue White Ambiance replacement using ESP32

**Note: This is work in progress, not guaranteed to be usable in any way.
If you want working (but minimal) example, see
[esp32-huello-world](https://github.com/wejn/esp32-huello-world) repo.**

This will be a firmware for my Hue White Ambiance replacement driver
suitable for my "Beng Ceiling Light" from Philips.

For background info see
[Reversing Philips Hue light driver](https://wejn.org/2024/12/reversing-philips-hue-light-driver/).

## Compilation

To compile:

``` sh
./in-docker.sh idf.py set-target esp32-c6 build
```

To clean up:

``` sh
./in-docker.sh rm -rf build dependencies.lock managed_components/ sdkconfig
```

To change your board's MAC (or other ZB parameters):

``` sh
./in-docker.sh python3 esp_zb_mfg_tool.py \
  --manufacturer_name Espressif --manufacturer_code 0x131B \
  --channel_mask 0x07FFF800 \
  --mac_address CAFEBEEF50C0FFA0
esptool.py write_flash 0x1d8000 ./bin/CAFEBEEF50C0FFA0.bin
```

Note: `0x1d8000` is the location of your `zb_fct` in `partitions.csv`.
And the other possible parameters (for `esp_zb_mfg_tool`) can be figured
out easily from its source.
