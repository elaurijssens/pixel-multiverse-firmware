#pragma once

// Multicast frame receiver (E7 S7.3). Joins an IPv4 multicast group (mgroup/mport
// from the k/v store) and reassembles self-describing chunk datagrams by offset
// into display::back() — best-effort: a new frame_id abandons an incomplete frame
// (E6 double-buffering keeps the last complete frame on screen). W builds only;
// no-op stubs otherwise.
//
// Chunk datagram (little-endian): magic "MVF1"(4) frame_id(2) flags(2)
// total_len(4) offset(4), then the chunk payload.

namespace net {
    // Join the group and start receiving. Call after WiFi is up.
    void multicast_init();

    // True once when a complete frame has landed in the back buffer since the last
    // call — the caller presents it (honouring hold/live). Clears the flag.
    bool multicast_take_frame();
}
