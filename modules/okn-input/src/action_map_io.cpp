// Binary (de)serialization of an ActionMap's key bindings. Format: magic + action
// count + the flat [n_actions][2] KeyCode table. The action-count guard makes load()
// reject a file authored for a different action set (e.g. a binding file from another
// game/version), so the caller keeps its defaults rather than loading garbage.

#include <okn/input/action_map.hpp>

#include <fstream>

namespace okn::input::detail {

namespace {
constexpr std::uint32_t kMagic = 0x4F4B4E49u;  // 'O','K','N','I'
}

bool save_bindings(const std::string& path, const KeyCode* keys, int n_actions) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { return false; }
    const std::uint32_t n = static_cast<std::uint32_t>(n_actions);
    f.write(reinterpret_cast<const char*>(&kMagic), sizeof(kMagic));
    f.write(reinterpret_cast<const char*>(&n), sizeof(n));
    f.write(reinterpret_cast<const char*>(keys),
            static_cast<std::streamsize>(sizeof(KeyCode) * 2 * static_cast<std::size_t>(n_actions)));
    return static_cast<bool>(f);
}

bool load_bindings(const std::string& path, KeyCode* keys, int n_actions) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { return false; }
    std::uint32_t magic = 0, n = 0;
    f.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    f.read(reinterpret_cast<char*>(&n), sizeof(n));
    if (!f || magic != kMagic || n != static_cast<std::uint32_t>(n_actions)) { return false; }
    f.read(reinterpret_cast<char*>(keys),
           static_cast<std::streamsize>(sizeof(KeyCode) * 2 * static_cast<std::size_t>(n_actions)));
    return static_cast<bool>(f);
}

}  // namespace okn::input::detail
