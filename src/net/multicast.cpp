#include "net/multicast.hpp"

#if MULTIVERSE_WIFI

#include "display.hpp"
#include "net/wifi.hpp"
#include "config/kv_flash.hpp"

#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "lwip/igmp.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/pbuf.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace net {
namespace {

constexpr size_t HDR = 16;                       // magic4 + fid2 + flags2 + total4 + off4
const uint8_t    MAGIC[4] = { 'M', 'V', 'F', '1' };

struct udp_pcb* g_pcb = nullptr;
uint16_t        cur_fid = 0;
bool            in_frame = false;
size_t          received = 0;
bool            frame_ready = false;             // set by the recv callback, taken by the loop

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Runs synchronously inside cyw43_arch_poll() (poll mode, no IRQ) → no locking.
void on_recv(void*, struct udp_pcb*, struct pbuf* p, const ip_addr_t*, u16_t) {
    if (p == nullptr) return;
    if (p->tot_len < HDR) { pbuf_free(p); return; }

    uint8_t hdr[HDR];
    pbuf_copy_partial(p, hdr, HDR, 0);
    if (memcmp(hdr, MAGIC, 4) != 0) { pbuf_free(p); return; }

    uint16_t fid   = le16(hdr + 4);
    uint32_t total = le32(hdr + 8);
    uint32_t off   = le32(hdr + 12);
    uint16_t clen  = static_cast<uint16_t>(p->tot_len - HDR);

    const size_t bufsz = display::buffer_size();
    if (total != bufsz || static_cast<size_t>(off) + clen > bufsz) {  // wrong size / OOB
        pbuf_free(p);
        return;
    }

    if (fid != cur_fid || !in_frame) {  // a new frame_id abandons any incomplete one
        cur_fid = fid;
        in_frame = true;
        received = 0;
    }
    pbuf_copy_partial(p, display::back() + off, clen, HDR);  // place the chunk by offset
    received += clen;
    if (received >= total) {   // best-effort completion (byte count; dups rare on a LAN)
        in_frame = false;
        frame_ready = true;
    }
    pbuf_free(p);
}

}  // namespace

void multicast_init() {
    if (!wifi_enabled() || netif_default == nullptr) return;

    char group[24];
    size_t vl = 0;
    const uint8_t* v = kv::config().get("mgroup", vl);
    if (v != nullptr && vl > 0 && vl < sizeof(group)) { memcpy(group, v, vl); group[vl] = 0; }
    else { std::strcpy(group, "239.255.0.1"); }

    uint16_t port = 54322;
    v = kv::config().get("mport", vl);
    if (v != nullptr && vl > 0) {
        long n = 0; bool ok = true;
        for (size_t i = 0; i < vl; i++) { if (v[i] < '0' || v[i] > '9') { ok = false; break; } n = n * 10 + (v[i] - '0'); }
        if (ok && n >= 1 && n <= 65535) port = static_cast<uint16_t>(n);
    }

    ip4_addr_t gaddr;
    if (!ip4addr_aton(group, &gaddr)) return;
    igmp_joingroup(netif_ip4_addr(netif_default), &gaddr);

    g_pcb = udp_new();
    if (g_pcb == nullptr) return;
    udp_bind(g_pcb, IP_ANY_TYPE, port);
    udp_recv(g_pcb, on_recv, nullptr);
}

bool multicast_take_frame() {
    if (!frame_ready) return false;
    frame_ready = false;
    return true;
}

}  // namespace net

#else  // !MULTIVERSE_WIFI

namespace net {
void multicast_init() {}
bool multicast_take_frame() { return false; }
}

#endif
