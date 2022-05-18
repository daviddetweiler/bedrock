#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

#include <gsl/gsl>

namespace bedrock {
	namespace {
		bool try_read_file(gsl::czstring<> filename, gsl::span<char> destination)
		{
			std::ifstream file {filename, file.ate | file.binary};
			try {
				file.exceptions(file.badbit | file.failbit);
				const auto size = file.tellg();
				if (size >= destination.size()) {
					std::cerr << "File too large for destination buffer\n";
					return false;
				}

				file.seekg(file.beg);
				file.read(destination.data(), size);
			}
			catch (const std::exception& error) {
				std::cerr << "Exception while reading file: '" << error.what() << "'\n";
				return false;
			}

			return true;
		}

		// 8-bit opcodes
		// 8-bit register IDs
		// 16-bit registers
		// Only unsigned math for now lol
		// 0 register

		enum class opcode : std::uint8_t {
			hypercall,

			move,
			load_immediate,
			load,
			store,

			bitwise_or,
			bitwise_and,
			bitwise_not,

			add,
			subtract,
			multiply,
			divide,

            shift_left,
            shift_right,

            // Still deciding how best to do control flow
			jump,
			jump_and_link,

            // 16 base instructions

            invalid
		};

        constexpr auto max_opcode = static_cast<std::uint8_t>(opcode::invalid);

		struct machine_state {
			std::uint16_t program_counter;
			std::vector<std::uint16_t> registers;
			std::vector<char> memory;

			machine_state() : registers(1 << 8), memory(1 << 16) {}
		};
	}
}

using namespace bedrock;

int main(int argc, char** argv)
{
	const gsl::span arguments {argv, gsl::narrow<std::size_t>(argc)};
	if (argc != 2) {
		std::cerr << "Usage: vm <image>\n";
		return 1;
	}

	machine_state state {};
	if (!try_read_file(arguments[1], state.memory)) {
		std::cerr << "Couldn't load image file '" << arguments[1] << "'\n";
		return 1;
	}
}
