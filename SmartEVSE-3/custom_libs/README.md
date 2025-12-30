# Custom mbedTLS Libraries

This folder contains modified mbedTLS static libraries (`.a` files) for ESP32.

## Purpose

These libraries have been modified to **reduce content length to 6144 bytes** and  **remove error strings**, which:
- Reduces RAM usage from ~58KB to ~36KB per TLS connection
- Reduces flash usage
- Has no functional impact (error codes still work, just no human-readable strings)

## Files

- `libmbedcrypto.a` - Cryptographic primitives
- `libmbedtls.a` - TLS protocol implementation
- `libmbedtls_2.a` - Secondary TLS library
- `libmbedx509.a` - X.509 certificate handling

## How it works

The `replace_mbedtls.py` script automatically copies these libraries to the PlatformIO
framework directory before building, replacing the default versions.

The original libraries are backed up with a `.original` extension the first time the
build runs.

## Compatibility

These libraries are built for:
- Platform: espressif32 @ 6.10.0
- ESP-IDF version bundled with the above platform

If you upgrade the espressif32 platform version, you may need to rebuild these
custom libraries.
