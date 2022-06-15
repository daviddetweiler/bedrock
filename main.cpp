#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>

namespace bedrock {
	namespace {
		using machine_word = std::uint16_t;
		constexpr auto word_size = sizeof(machine_word);
		constexpr auto max_word = std::numeric_limits<machine_word>::max();
		constexpr auto block_size = 512;
		constexpr auto block_words = block_size / word_size;
		constexpr auto disk_size = block_size * (1 << 16);

		enum class opcode : std::uint8_t {
			jump,
			read_high,
			set,
			load,
			store,
			add,
			subtract,
			multiply,
			divide,
			shift_left,
			shift_right,
			logic_and,
			logic_or,
			logic_not,
			bus_read,
			bus_write
		};

		struct instruction_word {
			opcode op;
			std::uint8_t destination;
			std::uint8_t source1;
			std::uint8_t source0;
		};

		struct disk_controller {
			std::fstream file;
			machine_word block_count;
			machine_word block;
			machine_word address;

			disk_controller(const char* path) : file {}, block_count {}, block {}, address {}
			{
				if (path) {
					file.exceptions(file.badbit | file.failbit);
					file.open(path, file.binary | file.ate | file.in | file.out);
					const auto n_blocks = file.tellg() / block_size;
					block_count = n_blocks < max_word ? static_cast<machine_word>(n_blocks) : max_word;
				}
			}
		};

		constexpr std::array<machine_word, 40> firmware_blob {
			0x2001, 0xeb00, 0x2b28, 0x2108, 0x0201, 0x210a, 0x0211, 0xc000, 0xf0c0, 0x00bb,
			0xe20c, 0x210a, 0x6021, 0x2110, 0x0001, 0x00bb, 0x203a, 0x8002, 0x2118, 0x0101,
			0x2030, 0x6002, 0x211a, 0x0111, 0x2057, 0x6002, 0x9f4f, 0xcf0f, 0x2201, 0x5ee2,
			0x2003, 0xb00e, 0x2126, 0x0101, 0x50bd, 0x40f0, 0x5d2d, 0xe00c, 0x210a, 0x0001};

		class memory_adapter {
		public:
			memory_adapter() : memory((1 << 16) - firmware_blob.size()) {}

			void write(machine_word address, machine_word word)
			{
				if (address >= firmware_blob.size())
					memory[address - firmware_blob.size()] = word;
			}

			auto read(machine_word address)
			{
				if (address >= firmware_blob.size())
					return memory[address - firmware_blob.size()];
				else
					return firmware_blob[address];
			}

		private:
			std::vector<machine_word> memory;
		};

		struct machine_state {
			machine_word instruction_pointer;
			machine_word high_word;
			std::array<machine_word, 1 << 4> registers;
			memory_adapter memory;
			disk_controller disk0;
			disk_controller disk1;
			bool halt;

			machine_state(const char* disk0_path, const char* disk1_path) :
				instruction_pointer {},
				high_word {},
				registers {},
				memory {},
				disk0 {disk0_path},
				disk1 {disk1_path},
				halt {false}
			{
			}
		};

		enum class disk_operation { read_block, write_block };

		void do_disk_operation(disk_controller& disk, memory_adapter& memory, machine_word control)
		{
			if (!disk.file.is_open())
				return;

			switch (static_cast<disk_operation>(control)) {
			case disk_operation::read_block:
				if (disk.block < disk.block_count) {
					disk.file.seekg(block_size * disk.block);
					for (auto i = 0u; i < block_words; ++i)
						memory.write(disk.address + i, disk.file.get() << 8 | disk.file.get());
				}

				break;

			case disk_operation::write_block:
				if (disk.block < disk.block_count) {
					disk.file.seekg(block_size * disk.block);
					for (auto i = 0u; i < block_size; ++i) {
						const auto word = memory.read(disk.address + i);
						disk.file.put(word >> 8);
						disk.file.put(word & 0xff);
					}
				}

				break;

			default:
				break;
			}
		}

		instruction_word decode(machine_word word) noexcept
		{
			const auto op = (word & 0xf000) >> 12;
			const auto destination = (word & 0x0f00) >> 8;
			const auto source1 = (word & 0x00f0) >> 4;
			const auto source0 = word & 0x000f;
			return {
				static_cast<opcode>(op),
				static_cast<std::uint8_t>(destination),
				static_cast<std::uint8_t>(source1),
				static_cast<std::uint8_t>(source0)};
		}

		void do_bus_read(machine_state& state, const instruction_word& instruction)
		{
			const auto port = state.registers[instruction.source0];
			switch (port) {
			case 0x0000:
				state.registers[instruction.destination] = std::cin.get() & 0xff;
				break;

			case 0x0001:
				state.registers[instruction.destination] = state.disk0.block_count;
				break;

			case 0x0002:
				state.registers[instruction.destination] = state.disk0.block;
				break;

			case 0x0003:
				state.registers[instruction.destination] = state.disk0.address;
				break;

			case 0x0004:
				state.registers[instruction.destination] = state.disk1.block_count;
				break;

			case 0x0005:
				state.registers[instruction.destination] = state.disk1.block;
				break;

			case 0x0006:
				state.registers[instruction.destination] = state.disk1.address;
				break;

			default:
				state.registers[instruction.destination] = 0;
				break;
			}
		}

		void do_bus_write(machine_state& state, const instruction_word& instruction)
		{
			const auto port = state.registers[instruction.source0];
			const auto word = state.registers[instruction.source1];
			switch (port) {
			case 0x0000:
				std::cout.put(word & 0xff);
				break;

			case 0x0001:
				do_disk_operation(state.disk0, state.memory, word);
				break;

			case 0x0002:
				state.disk0.block = word;
				break;

			case 0x0003:
				state.disk0.address = word;
				break;

			case 0x0004:
				do_disk_operation(state.disk1, state.memory, word);
				break;

			case 0x0005:
				state.disk1.block = word;
				break;

			case 0x0006:
				state.disk1.address = word;
				break;

			case 0x0007:
				state.halt = word;
				break;

			default:
				break;
			}
		}

		void execute(machine_state& state)
		{
			while (!state.halt) {
				const auto instruction = decode(state.memory.read(state.instruction_pointer++));
				switch (instruction.op) {
				case opcode::jump:
					if (state.registers[instruction.source1]) {
						const auto link = state.instruction_pointer;
						state.instruction_pointer = state.registers[instruction.source0];
						state.registers[instruction.destination] = link;
					}

					break;

				case opcode::read_high:
					state.registers[instruction.destination] = state.high_word;
					break;

				case opcode::set:
					state.registers[instruction.destination] = instruction.source1 << 4 | instruction.source0;
					break;

				case opcode::load:
					state.registers[instruction.destination] = state.memory.read(state.registers[instruction.source0]);
					break;

				case opcode::store:
					state.memory.write(state.registers[instruction.source0], state.registers[instruction.source1]);
					break;

				case opcode::add: {
					const std::uint32_t a {state.registers[instruction.source0]};
					const std::uint32_t b {state.registers[instruction.source1]};
					const auto c = a + b;
					state.registers[instruction.destination] = c & 0xffff;
					state.high_word = c >> 16;
					break;
				}

				case opcode::subtract: {
					const std::uint32_t a {state.registers[instruction.source0]};
					const std::uint32_t b {state.registers[instruction.source1]};
					const auto c = a - b;
					state.registers[instruction.destination] = c & 0xffff;
					state.high_word = c >> 16;
					break;
				}

				case opcode::multiply: {
					const std::uint32_t a {state.registers[instruction.source0]};
					const std::uint32_t b {state.registers[instruction.source1]};
					const auto c = a * b;
					state.registers[instruction.destination] = c & 0xffff;
					state.high_word = c >> 16;
					break;
				}

				case opcode::divide: {
					const std::uint32_t a {state.registers[instruction.source0]};
					const std::uint32_t b {state.registers[instruction.source1]};
					const std::uint32_t c {b ? a / b : 0xffffffff};
					state.registers[instruction.destination] = c & 0xffff;
					state.high_word = c >> 16;
					break;
				}

				case opcode::shift_left:
					state.registers[instruction.destination] = state.registers[instruction.source0]
						<< instruction.source1;

					break;

				case opcode::shift_right:
					state.registers[instruction.destination]
						= state.registers[instruction.source0] >> instruction.source1;

					break;

				case opcode::logic_and:
					state.registers[instruction.destination]
						= state.registers[instruction.source0] & state.registers[instruction.source1];

					break;

				case opcode::logic_or:
					state.registers[instruction.destination]
						= state.registers[instruction.source0] | state.registers[instruction.source1];

					break;

				case opcode::logic_not:
					state.registers[instruction.destination] = ~state.registers[instruction.source0];
					break;

				case opcode::bus_read:
					do_bus_read(state, instruction);
					break;

				case opcode::bus_write:
					do_bus_write(state, instruction);
					break;
				}
			}
		}
	}
}

using namespace bedrock;

int main(int argc, char** argv)
{
	if (argc != 3) {
		std::cout << "Usage: bedrock <disk0> <disk1>\n";
		std::cout << "Use -- to omit a disk file.\n";
		return 0;
	}

	const auto nullptr_if_none = [](auto path) { return std::strcmp(path, "--") == 0 ? nullptr : path; };
	const auto check_path = [](auto path) {
		if (!path || std::filesystem::exists(path))
			return true;

		std::cerr << "File \"" << path << "\" does not exist.\n";

		return false;
	};

	const auto disk0 = nullptr_if_none(argv[1]);
	const auto disk1 = nullptr_if_none(argv[2]);
	if (!(check_path(disk0) && check_path(disk1)))
		return 1;

	try {
		machine_state state {disk0, disk1};
		execute(state);
	}
	catch (std::exception& error) {
		std::cerr << "Encountered fatal error: \"" << error.what() << "\"\n";
		return 1;
	}
}
