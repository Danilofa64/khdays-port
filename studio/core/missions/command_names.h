// The game's script commands by the functions it runs for them: the interpreter's handler at
// data_020425ec[group] + command * 8, named from the pinned decompilation's tables
// (studio/tools/command_names.py writes command_names.cpp). A name says which function runs, not yet
// what it does with each operand.
#pragma once

#include <cstddef>

namespace studio {

struct CommandName {
    int group;
    int command;
    const char* handler;       // "" when the table's handler is null (the step is skipped)
    const char* continuation;  // the second word: run each frame after a handler returns 0
};

// null when no table of the decompilation names it
const CommandName* command_name(int group, int command);
std::size_t command_name_count();

}  // namespace studio
