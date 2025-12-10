# KFDtool-ESP32 Standalone Keyloader (WT32-SC01-PLUS)

This is a **PlatformIO** project that gives you a starting point for a **standalone keyloader** inspired by the open-source [KFDtool](https://github.com/KFDtool/KFDtool) project, but running entirely on an **ESP32-S3 WT32-SC01-PLUS** with a touch LCD.

This version fixes:
- LVGL font usage (uses `LV_FONT_DEFAULT` so no external font config is needed)
- PBKDF2 implementation (manual PBKDF2-HMAC-SHA256 using mbedTLS HMAC API instead of `mbedtls_pkcs5_pbkdf2_hmac`)
- LovyanGFX dependency via direct GitHub URL in `lib_deps`

> ⚠️ **Important**  
> This repo is a *framework*, not a full P25-compliant implementation.  
> You will still need to port the actual P25 KFD protocol state machine from KFDtool (or your own implementation) into `kfd_protocol.*`, and align the key container format with whatever you use on your desktop tooling.

## Quickstart

1. Open this folder in **VS Code + PlatformIO**.
2. Run **Build** for `wt32-sc01-plus`.
3. Flash to your WT32-SC01-PLUS board.
4. You should see the LVGL UI with three main buttons:
   - Key Containers
   - Keyload to Radio
   - Security Settings

Once that is working, we can iterate on:
- Proper KFD frame formats and timing (using KFDtool as a reference)
- PIN/PASSPHRASE input for unlocking containers
- More detailed UI for selecting containers and keys.
