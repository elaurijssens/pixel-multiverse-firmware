#pragma once

#include <cstdint>

// Multicast frame receiver (E7 S7.3). Joins an IPv4 multicast group (mgroup/mport
// from the k/v store) and reassembles self-describing chunk datagrams by offset
// into display::back() — best-effort: a new frame_id abandons an incomplete frame
// (E6 double-buffering keeps the last complete frame on screen). W builds only;
// no-op stubs otherwise.
//
// Chunk datagram (little-endian): magic "MVF1"(4) frame_id(2) flags(2)
// total_len(4) offset(4), then the chunk payload.
//
// S7.4 sync flip: a datagram with flags bit 0 (FLIP) set carries no payload — every
// board that receives it presents its back buffer, so one multicast packet flips a
// whole wall together. Host pattern: `hold` each board, unicast each its frame slice,
// then send one multicast FLIP to the group.

namespace net {
    // Join the group and start receiving. Call after WiFi is up.
    void multicast_init();

    // True once when a complete frame has landed in the back buffer since the last
    // call — the caller presents it (honouring hold/live). Clears the flag.
    bool multicast_take_frame();

    // True once when a multicast FLIP datagram (S7.4) arrived since the last call —
    // the caller presents the back buffer unconditionally. Clears the flag.
    bool multicast_take_flip();

    // Diagnostics (cumulative since boot): frames completed, frames abandoned incomplete
    // (a lost chunk), and the slowest zlib decompress seen in µs. Zeroed on non-W builds.
    void multicast_stats(uint32_t& frames, uint32_t& dropped, uint32_t& dmax_us);

    // Sync-flip diagnostics: flips that presented vs flips gated because the frame wasn't
    // ready in time (each gate = a skipped present on that board → inter-board skew).
    void multicast_flip_stats(uint32_t& ok, uint32_t& gated);
}
