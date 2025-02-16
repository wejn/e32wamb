# ESP32 Lightbulb example that works with Hue bridge

This is Espressif's [HA_color_dimmable_light](https://github.com/espressif/esp-zigbee-sdk/tree/a943a9118e9ad110e2641e1187fd4c5d533f8a06/examples/esp_zigbee_HA_sample/HA_color_dimmable_light)
adjusted to successfully link against Hue bridge, and function
like a properly behaved light (mostly).

Mind you, you'll need to adjust the trust center link key
(heed the `FIXME` in `main/esp_zb_light.c`).

Details in my [Zigbee: Hue-llo world!](https://wejn.org/2025/01/zigbee-hue-llo-world/)
blog post.

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
