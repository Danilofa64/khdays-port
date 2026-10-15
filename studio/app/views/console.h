// The tool is a windowed program, so opening it from the desktop shows no
// console. When it was started from a terminal (to run its command line),
// attach_parent_console connects to that terminal so its output is seen there.
#pragma once

namespace studio {

void attach_parent_console();

}  // namespace studio
