#pragma once

void screen_selftest(void);
void screen_ap_mode(const char *ap_ssid, const char *url);
void screen_sta_ready(const char *ssid, const char *ip);
void screen_sta_failed(const char *ssid, const char *reason);
