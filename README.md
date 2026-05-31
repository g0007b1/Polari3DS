# Polari3DS (this fork)

My fork of **[Polari3DS](https://github.com/Alexyo21/Polari3DS)** with additional features:

- **Extended raw luminance ceiling** — configurable cap via `POLARI_ROSALINA_BRIGHTNESS_TRUE_MAX` in Rosalina (`include/luminance.h`).
- **Separate top / bottom brightness tables** — bottom presets persist in `/luma/polari_bot_lum.bin` (CFG block `0x50002` is too small for both rows).
- **Permanent brightness recalibration** — edit per-screen preset rows; START saves CFG (top) + SD file (bottom).
- **Startup split mapping** — after boot, maps the current global brightness to the bottom curve via MMIO (`setBrightnessAlt`) so the lower screen can follow its own min/max without unsafe `gspLcdInit` during early init; SD load is retried so the bottom file is not missed if the filesystem is not ready on the first read.

Upstream Polari3DS already bundles many Rosalina extras (streaming, shortcuts, volume, etc.); see the [official Polari3DS repository](https://github.com/Alexyo21/Polari3DS) for the full upstream list and builds.

Краткая история:
Купил б/у nintendo 3ds xl, у которого на верхнем экране яркость сильно занижена (скорее всего из за замены, либо из за старости), и ни одна из прошивок не давала повысить яркость экранов свыше предела, тем более отдельно нижнего отдельно верхнего. Данная прошивка позволяет повышать мощность на каждый из экранов отдельно сверх максимума (500 единиц вместо 170). Настройки сохраняются с перезагрузкой, и работает на уровне железа, позволяя запускать ds игры также с повышенной яркостью. Минусы - быстрее разряжается батарейка. Надеюсь этот форк поможет кому нибудь также с проблемой яркости экранов.
