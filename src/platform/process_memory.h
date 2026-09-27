#pragma once
#include <cstdint>
#include <optional>

struct ScProcessMemory {
    std::optional<std::uint64_t> resident, peak_resident;
};
ScProcessMemory sc_process_memory();
