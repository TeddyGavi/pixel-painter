#pragma once
#include <stddef.h>
#include <stdint.h>

// QR export: URL to the static export page with the pattern in the fragment.
int export_url(char *out, size_t cap);   // length, or -1 if it cannot be encoded
const uint8_t *export_qr(void);          // qrcodegen buffer, or NULL (too large)

// Wi-Fi fallback: SoftAP + web server while the Wi-Fi screen is shown.
void export_start(void);
void export_stop(void);
