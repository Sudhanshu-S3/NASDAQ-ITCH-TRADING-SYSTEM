#include "common/file_buffer.h"

#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>

namespace nts
{
    std::vector<std::byte> read_file(std::string_view path)
    {
        // std::string rather than the view, because ifstream needs a null terminated
        // path and a string_view carries no terminator.
        const std::string name{path};

        // ate seeks to the end on open, so tellg gives the size without a second seek.
        std::ifstream file(name, std::ios::binary | std::ios::ate);
        if (!file)
        {
            throw std::runtime_error("failed to open file: " + name);
        }

        const std::streampos size = file.tellg();
        if (size < 0)
        {
            throw std::runtime_error("failed to determine size of: " + name);
        }

        std::vector<std::byte> buffer(static_cast<std::size_t>(size));
        file.seekg(0, std::ios::beg);

        if (!file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size)))
        {
            throw std::runtime_error("failed to read contents of: " + name);
        }

        return buffer;
    }
}  // namespace nts
