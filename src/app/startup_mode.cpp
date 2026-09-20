#include "startup_mode.h"

#include <cwchar>
#include <string>

namespace ptd {

StartupMode parse_startup_mode(std::wstring_view command_line) {
    const std::vector<std::wstring> args = split_command_line(std::wstring(command_line));

    // argv[0] is the executable path (quoted or not) and is never an
    // argument. Everything after it is scanned.
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (_wcsicmp(args[i].c_str(), kStartupMinimizedArgument) == 0) {
            return StartupMode::AutostartMinimized;
        }
    }
    return StartupMode::Normal;
}

} // namespace ptd
