#include "net/multicast.hpp"

#if MULTIVERSE_WIFI

#include "display.hpp"
#include "net/wifi.hpp"
#include "config/kv_flash.hpp"

#include "pico/cyw43_arch.h"
#include "pico/time.h"
#include "lwip/udp.h"
#include "lwip/igmp.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/pbuf.h"
#include "zlib.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>

namespace net {
namespace {

constexpr size_t   HDR = 16;                     // magic4 + fid2 + flags2 + total4 + off4
const uint8_t      MAGIC[4] = { 'M', 'V', 'F', '1' };
constexpr uint16_t FLAG_FLIP = 0x0001;           // S7.4: present the back buffer (no payload)
constexpr uint16_t FLAG_ZLIB = 0x0002;           // payload is a zlib-compressed frame (total =
                                                 // compressed size); decompress into back() on completion

// Redundancy: the host may send each chunk more than once so a single lost packet
// doesn't drop the frame. Completion tracks *which* offsets arrived (not a byte count),
// so a duplicate is idempotent and a chunk lost in one copy is filled by the next. The
// table is tiny — a compressed frame is ~8 chunks, a raw one ~46; 128 covers any real case.
constexpr size_t MAX_CHUNKS = 128;

struct udp_pcb* g_pcb = nullptr;
uint16_t        cur_fid = 0;
bool            in_frame = false;
uint16_t        done_fid = 0;                    // last fid that fully completed…
bool            have_done = false;               // …so redundant copies arriving after it are ignored
bool            cur_zlib = false;                // is the in-flight frame zlib-compressed?
uint32_t        seen_off[MAX_CHUNKS];            // offsets already placed for the current frame
size_t          seen_n = 0;
size_t          received = 0;
bool            frame_ready = false;             // set by the recv callback, taken by the loop
bool            flip_ready = false;              // S7.4: a multicast FLIP datagram arrived
bool            back_complete = false;           // S7.4: a *whole* frame is loaded in back(),
                                                 // not yet presented — gate the flip on this so
                                                 // a partial (mid-load) buffer never tears
uint8_t*        zbuf = nullptr;                  // scratch to reassemble a compressed blob before
size_t          zbuf_cap = 0;                    // decompressing it into back() (alloc'd at init)

// Diagnostics: pin down where frames go missing (air loss vs board-side stall).
uint32_t        stat_frames = 0;                 // frames completed + presented
uint32_t        stat_dropped = 0;                // frames abandoned incomplete (new fid too soon)
uint32_t        stat_dmax_us = 0;                // slowest zlib decompress seen (µs)
uint32_t        stat_flip_ok = 0;                // sync flips that presented a ready frame
uint32_t        stat_flip_gate = 0;              // sync flips gated (frame not ready → held; skew)

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

    uint16_t flags = le16(hdr + 6);
    if (flags & FLAG_FLIP) {   // S7.4 sync flip: no payload — the loop presents the back buffer
        flip_ready = true;
        pbuf_free(p);
        return;
    }

    bool     zlib  = (flags & FLAG_ZLIB) != 0;
    uint16_t fid   = le16(hdr + 4);
    uint32_t total = le32(hdr + 8);
    uint32_t off   = le32(hdr + 12);
    uint16_t clen  = static_cast<uint16_t>(p->tot_len - HDR);

    const size_t bufsz = display::buffer_size();
    // Raw: `total` is the frame size and must equal the buffer. Compressed: `total` is the
    // compressed blob size — reassembled in `zbuf`, then decompressed into back(). A frame
    // that doesn't shrink is sent raw by the host, so compressed total <= bufsz always holds.
    if (zlib) {
        if (zbuf == nullptr || total == 0 || total > zbuf_cap ||
            static_cast<size_t>(off) + clen > total) { pbuf_free(p); return; }
    } else {
        if (total != bufsz || static_cast<size_t>(off) + clen > bufsz) { pbuf_free(p); return; }
    }

    // Redundant copies of an already-completed frame keep arriving (2× send) with the same
    // fid — ignore them, or the !in_frame test below would treat them as a new frame and
    // reassemble it all over again (double decompress, and a back_complete flicker that lets
    // a sync flip land in the gap and drop).
    if (have_done && fid == done_fid) { pbuf_free(p); return; }

    if (fid != cur_fid || !in_frame) {  // a new frame_id abandons any incomplete one
        if (in_frame) stat_dropped++;   // the previous frame never completed (lost a chunk)
        cur_fid = fid;
        in_frame = true;
        received = 0;
        seen_n = 0;
        cur_zlib = zlib;
        back_complete = false;   // back() is being overwritten — no clean frame to flip yet
    }
    // Drop a chunk offset we've already placed (a redundant/duplicate send) so it isn't
    // counted twice toward completion — that's what makes 2× sending fill single-packet
    // gaps. Table overflow just stops deduping (safe; unreachable at real chunk sizes).
    for (size_t i = 0; i < seen_n; i++) {
        if (seen_off[i] == off) { pbuf_free(p); return; }
    }
    if (seen_n < MAX_CHUNKS) seen_off[seen_n++] = off;

    uint8_t* dst = cur_zlib ? zbuf : display::back();
    pbuf_copy_partial(p, dst + off, clen, HDR);  // place the chunk by offset
    received += clen;
    if (received >= total) {   // completion by unique-offset coverage (dups already dropped)
        in_frame = false;
        done_fid = cur_fid;      // remember it so this frame's redundant copies are ignored
        have_done = true;
        if (cur_zlib) {          // decompress the reassembled blob straight into back()
            uLongf dlen = bufsz;
            uint32_t t0 = time_us_32();
            int r = uncompress(display::back(), &dlen, zbuf, total);
            uint32_t dt = time_us_32() - t0;
            if (dt > stat_dmax_us) stat_dmax_us = dt;
            if (r == Z_OK && dlen == bufsz) {
                frame_ready = true;
                back_complete = true;
                stat_frames++;
            }
            // else: a lost chunk left the blob corrupt/short — leave back() as the last good
            // frame and drop this one (best-effort; far rarer now — fewer chunks per frame)
        } else {
            frame_ready = true;
            back_complete = true;    // a whole frame now sits in back() — safe to flip (S7.4)
            stat_frames++;
        }
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

    // Scratch for reassembling a compressed blob before decompressing into back().
    // A frame that doesn't shrink is sent raw, so the blob never exceeds a raw frame.
    zbuf_cap = display::buffer_size();
    zbuf = static_cast<uint8_t*>(malloc(zbuf_cap));  // one-time alloc; no per-frame churn

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

bool multicast_take_flip() {
    if (!flip_ready) return false;
    flip_ready = false;
    if (!back_complete) {              // partial/mid-load frame — hold, don't tear (S7.4 gate)
        stat_flip_gate++;             // …but a gated flip means this board skipped a present → skew
        return false;
    }
    stat_flip_ok++;
    back_complete = false;
    return true;
}

void multicast_stats(uint32_t& frames, uint32_t& dropped, uint32_t& dmax_us) {
    frames = stat_frames;
    dropped = stat_dropped;
    dmax_us = stat_dmax_us;
}

void multicast_flip_stats(uint32_t& ok, uint32_t& gated) {
    ok = stat_flip_ok;
    gated = stat_flip_gate;
}

}  // namespace net

#else  // !MULTIVERSE_WIFI

namespace net {
void multicast_init() {}
bool multicast_take_frame() { return false; }
bool multicast_take_flip() { return false; }
void multicast_stats(uint32_t&, uint32_t&, uint32_t&) {}
void multicast_flip_stats(uint32_t&, uint32_t&) {}
}

#endif
