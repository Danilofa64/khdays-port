// The command line: commands that run without a
// window, print what they did to stdout and problems to stderr, and exit 0
// on success, 1 on a problem, 2 on bad arguments.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace studio {

// Takes `--port <dir>` out of `args` into `root`; false when it has no folder.
bool take_port_argument(std::vector<std::string>& args, std::filesystem::path* root);

// Runs the command `args` names; the process's exit code.
int run_command_line(const std::filesystem::path& root, const std::vector<std::string>& args);

}  // namespace studio
