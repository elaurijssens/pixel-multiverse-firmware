#include "config/kv_commands.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "config/kv_flash.hpp"
#include "config/kv_format.hpp"
#include "command/command_core.hpp"
#include "command/transport.hpp"

// Wire protocol (length-prefixed; all responses status-first):
//   put : klen(1) key[klen] vlen(1) value[vlen] -> status(1)  (1 ok / 0 fail)
//   get : klen(1) key[klen]                      -> 1, vlen(1), value[vlen]  |  0
//   del : klen(1) key[klen]                      -> status(1)  (1 deleted / 0 absent)
// Malformed frames answer status 0; the command loop re-syncs on the next prefix.
namespace kv {
namespace {

using command_core::Transport;

size_t read_bytes(Transport& t, uint8_t* buf, size_t n) {
    t.poll();
    return t.read(buf, n);
}

bool read_u8(Transport& t, uint8_t& out) {
    return read_bytes(t, &out, 1) == 1;
}

void write_u8(Transport& t, uint8_t v) {
    t.write(&v, 1);
}

// Read klen(1) + key bytes into `key` (capacity KEY_LEN). Returns the key length,
// or 0 if the length is out of range or a read timed out.
size_t read_key(Transport& t, char* key) {
    uint8_t klen;
    if (!read_u8(t, klen) || klen < 1 || klen > KEY_LEN) return 0;
    if (read_bytes(t, reinterpret_cast<uint8_t*>(key), klen) != klen) return 0;
    return klen;
}

// Secret keys whose value must never leave the board in the clear (WiFi password).
bool is_secret_key(const char* key, size_t klen) {
    return klen == 4 && key[0] == 'p' && key[1] == 'a' && key[2] == 's' && key[3] == 's';
}

void handle_put(Transport& t) {
    char key[KEY_LEN];
    size_t klen = read_key(t, key);
    uint8_t vlen = 0;
    uint8_t value[VALUE_LEN];
    bool ok = false;
    if (klen > 0 && read_u8(t, vlen) && vlen <= VALUE_LEN &&
        (vlen == 0 || read_bytes(t, value, vlen) == vlen)) {
        ok = config_put(std::string_view(key, klen), value, vlen);
    }
    write_u8(t, ok ? 1 : 0);
}

void handle_get(Transport& t) {
    char key[KEY_LEN];
    size_t klen = read_key(t, key);
    if (klen == 0) { write_u8(t, 0); return; }

    size_t vlen = 0;
    const uint8_t* value = config().get(std::string_view(key, klen), vlen);
    if (!value) { write_u8(t, 0); return; }

    // Never return a secret's value in the clear — report it as set but hidden.
    if (is_secret_key(key, klen)) {
        static const char hidden[] = "<hidden>";
        write_u8(t, 1);
        write_u8(t, static_cast<uint8_t>(sizeof(hidden) - 1));
        t.write(reinterpret_cast<const uint8_t*>(hidden), sizeof(hidden) - 1);
        return;
    }

    write_u8(t, 1);
    write_u8(t, static_cast<uint8_t>(vlen));
    if (vlen) t.write(value, vlen);
}

void handle_del(Transport& t) {
    char key[KEY_LEN];
    size_t klen = read_key(t, key);
    bool ok = klen > 0 && config_del(std::string_view(key, klen));
    write_u8(t, ok ? 1 : 0);
}

// Diagnostics: dump the live store as a u16 length-prefixed ASCII blob, one
// "key=value" line per record. Non-printable value bytes render as '.'; secret
// keys (e.g. `pass`) are masked so the dump never leaks credentials.
void handle_keys(Transport& t) {
    static char buf[2560];
    size_t pos = 0;
    size_t n = config().count();
    for (size_t i = 0; i < n; i++) {
        const Record& r = config().at(i);
        for (size_t k = 0; k < r.key_len && pos < sizeof(buf); k++) buf[pos++] = r.key[k];
        if (pos < sizeof(buf)) buf[pos++] = '=';
        if (is_secret_key(r.key, r.key_len)) {
            const char* masked = (r.value_len > 0) ? "<hidden>" : "";
            for (size_t j = 0; masked[j] && pos < sizeof(buf); j++) buf[pos++] = masked[j];
        } else {
            for (size_t v = 0; v < r.value_len && pos < sizeof(buf); v++) {
                uint8_t c = r.value[v];
                buf[pos++] = (c >= 0x20 && c < 0x7f) ? static_cast<char>(c) : '.';
            }
        }
        if (pos < sizeof(buf)) buf[pos++] = '\n';
    }
    uint8_t hdr[2] = { static_cast<uint8_t>(pos & 0xff), static_cast<uint8_t>((pos >> 8) & 0xff) };
    t.write(hdr, 2);
    if (pos) t.write(reinterpret_cast<const uint8_t*>(buf), pos);
}

} // namespace

void register_commands() {
    command_core::register_command("put ", handle_put);
    command_core::register_command("get ", handle_get);
    command_core::register_command("del ", handle_del);
    command_core::register_command("keys", handle_keys);
}

} // namespace kv
