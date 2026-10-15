// Writing into a mod safely: beside the destination under a temporary name that keeps its
// extension (the port decodes some kinds by it), checked as the port will read
// it, then put in place. A failure leaves what was there before.
#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace studio {

struct WriteResult {
    bool ok = false;
    bool outside = false;  // the destination is not inside the mod: nothing done
    bool locked = false;   // the system refused to replace it (the game holds it open?)
    std::string why;
};

// `produce` writes the temporary file and `verify` checks it; each returns an
// empty string, or the reason it failed.
WriteResult write_into_mod(const std::filesystem::path& mod_dir, const std::filesystem::path& destination,
                           const std::function<std::string(const std::filesystem::path& temporary)>& produce,
                           const std::function<std::string(const std::filesystem::path& temporary)>& verify);

// Copies a file byte for byte (paths in any script); empty or the reason.
std::string copy_bytes(const std::filesystem::path& from, const std::filesystem::path& to);

}  // namespace studio
