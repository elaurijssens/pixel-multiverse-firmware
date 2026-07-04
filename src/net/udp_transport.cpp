#include "net/udp_transport.hpp"

#if MULTIVERSE_WIFI

#include "net/wifi.hpp"
#include "config/kv_flash.hpp"

#include "pico/cyw43_arch.h"
#include "pico/time.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"

#include <cstddef>
#include <cstdint>

namespace net {
namespace {

// A byte-stream Transport backed by a ring buffer that the lwIP UDP receive
// callback fills. read()/wait_for() drain it, pumping cyw43_arch_poll() (which
// dispatches the callback) until enough bytes arrive or the timeout elapses.
class UdpTransport : public command_core::Transport {
public:
    void init(uint16_t port) {
        pcb_ = udp_new();
        if (pcb_ == nullptr) return;
        udp_bind(pcb_, IP_ANY_TYPE, port);
        udp_recv(pcb_, &UdpTransport::recv_cb, this);
    }

    void poll() override { cyw43_arch_poll(); }

    bool wait_for(std::string_view data, uint32_t timeout_ms) override {
        if (data.empty()) return true;
        absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
        size_t idx = 0;
        for (;;) {
            while (count_ > 0) {
                uint8_t c = pop();
                if (c == static_cast<uint8_t>(data[idx])) {
                    if (++idx == data.size()) return true;
                } else {
                    idx = (c == static_cast<uint8_t>(data[0])) ? 1 : 0;
                }
            }
            if (time_reached(deadline)) return false;
            cyw43_arch_poll();
        }
    }

    size_t read(uint8_t* buffer, size_t len, uint32_t timeout_ms) override {
        for (size_t i = 0; i < len; i++) buffer[i] = 0;  // zero first (per contract)
        absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
        size_t got = 0;
        for (;;) {
            while (count_ > 0 && got < len) buffer[got++] = pop();
            if (got == len || time_reached(deadline)) return got;
            cyw43_arch_poll();
        }
    }

    size_t write(const uint8_t* buffer, size_t len) override {
        if (pcb_ == nullptr || !have_peer_) return 0;
        struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, static_cast<u16_t>(len), PBUF_RAM);
        if (p == nullptr) return 0;
        pbuf_take(p, buffer, static_cast<u16_t>(len));
        err_t e = udp_sendto(pcb_, p, &peer_, peer_port_);
        pbuf_free(p);
        return (e == ERR_OK) ? len : 0;
    }

private:
    static constexpr size_t RB = 8192;   // reassembly ring (drains as read() consumes)
    uint8_t  buf_[RB];
    size_t   head_  = 0;
    size_t   count_ = 0;

    void push(const uint8_t* d, size_t n) {
        for (size_t i = 0; i < n; i++) {
            if (count_ == RB) return;    // full → drop the tail of the burst
            buf_[(head_ + count_) % RB] = d[i];
            count_++;
        }
    }
    uint8_t pop() { uint8_t c = buf_[head_]; head_ = (head_ + 1) % RB; count_--; return c; }

    struct udp_pcb* pcb_ = nullptr;
    ip_addr_t       peer_{};
    u16_t           peer_port_ = 0;
    bool            have_peer_ = false;

    static void recv_cb(void* arg, struct udp_pcb*, struct pbuf* p,
                        const ip_addr_t* addr, u16_t port) {
        if (p != nullptr) static_cast<UdpTransport*>(arg)->on_recv(p, addr, port);
    }
    void on_recv(struct pbuf* p, const ip_addr_t* addr, u16_t port) {
        peer_ = *addr; peer_port_ = port; have_peer_ = true;
        for (struct pbuf* q = p; q != nullptr; q = q->next) {
            push(static_cast<const uint8_t*>(q->payload), q->len);
        }
        pbuf_free(p);
    }
};

UdpTransport g_udp;

}  // namespace

command_core::Transport* udp_transport() { return &g_udp; }

void udp_transport_init() {
    if (!wifi_enabled()) return;   // no lwIP netif without WiFi up
    uint16_t port = 54321;
    size_t vlen = 0;
    const uint8_t* v = kv::config().get("port", vlen);
    if (v != nullptr && vlen > 0) {
        long n = 0;
        bool ok = true;
        for (size_t i = 0; i < vlen; i++) {
            if (v[i] < '0' || v[i] > '9') { ok = false; break; }
            n = n * 10 + (v[i] - '0');
        }
        if (ok && n >= 1 && n <= 65535) port = static_cast<uint16_t>(n);
    }
    g_udp.init(port);
}

}  // namespace net

#else  // !MULTIVERSE_WIFI — no lwIP; no UDP transport

namespace net {
command_core::Transport* udp_transport() { return nullptr; }
void udp_transport_init() {}
}

#endif
