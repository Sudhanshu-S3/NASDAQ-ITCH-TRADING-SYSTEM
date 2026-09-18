#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace nts
{
    /**
     * Reads an entire file into memory as raw bytes.
     */
    std::vector<std::byte> read_file(std::string_view path);
}  // namespace nts
