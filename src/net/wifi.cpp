#include "net/wifi.hpp"

#if MULTIVERSE_WIFI

#include "pico/cyw43_arch.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "config/kv_flash.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace net {
namespace {
    bool enabled = false;

    // Copy a k/v value into a null-terminated buffer (cap-1 chars max).
    void read_str(const char* key, char* out, size_t cap) {
        out[0] = 0;
        size_t vlen = 0;
        const uint8_t* v = kv::config().get(key, vlen);
        if (v == nullptr || cap == 0) return;
        if (vlen > cap - 1) vlen = cap - 1;
        memcpy(out, v, vlen);
        out[vlen] = 0;
    }

    bool key_truthy(const char* key) {
        char b[8];
        read_str(key, b, sizeof(b));
        switch (b[0]) { case '1': case 'y': case 'Y': case 't': case 'T': case 'o': case 'O': return true; }
        return false;
    }

    uint32_t read_auth() {
        char a[8];
        read_str("auth", a, sizeof(a));
        if (strcmp(a, "wpa3")  == 0) return CYW43_AUTH_WPA3_SAE_AES_PSK;
        if (strcmp(a, "mixed") == 0) return CYW43_AUTH_WPA3_WPA2_AES_PSK;
        if (strcmp(a, "open")  == 0) return CYW43_AUTH_OPEN;
        return CYW43_AUTH_WPA2_AES_PSK;  // default / "wpa2"
    }
}

void wifi_init() {
    if (!key_truthy("wifi")) return;       // disabled → behave as a non-wifi board

    if (cyw43_arch_init() != 0) return;    // no chip / init failure — stay disabled
    enabled = true;
    cyw43_arch_enable_sta_mode();

    char ssid[33], pass[64];
    read_str("ssid", ssid, sizeof(ssid));
    read_str("pass", pass, sizeof(pass));
    if (ssid[0] == 0) return;              // enabled but no ssid to join

    // Async connect — driven by wifi_poll() from the command loop, so neither boot
    // nor USB ever blocks on the association / DHCP.
    cyw43_arch_wifi_connect_async(ssid, pass, read_auth());
}

void wifi_poll() {
    if (enabled) cyw43_arch_poll();
}

bool wifi_enabled() { return enabled; }

bool wifi_connected() {
    return enabled && cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP;
}

const char* wifi_ip() {
    if (wifi_connected() && netif_default != nullptr) {
        const ip4_addr_t* ip = netif_ip4_addr(netif_default);
        if (ip != nullptr && ip->addr != 0) return ip4addr_ntoa(ip);
    }
    return "";
}

} // namespace net

#else  // !MULTIVERSE_WIFI — no-op stubs so callers need no guards

namespace net {
void wifi_init() {}
void wifi_poll() {}
bool wifi_enabled()   { return false; }
bool wifi_connected() { return false; }
const char* wifi_ip() { return ""; }
}

#endif
