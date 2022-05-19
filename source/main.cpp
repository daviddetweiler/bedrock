#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <iostream>
#include <vector>

#include <gsl/gsl>

namespace bedrock {
	namespace {
		bool try_read_file(gsl::czstring filename, gsl::span<std::byte> destination)
		{
			std::ifstream file {filename, file.ate | file.binary};
			try {
				file.exceptions(file.badbit | file.failbit);
				const auto size = file.tellg();
				if (size >= destination.size()) {
					std::cerr << std::format(
						"File too large for destination buffer ({}, {})\n",
						gsl::narrow<std::size_t>(size),
						destination.size());

					return false;
				}

				file.seekg(file.beg);
				file.read(reinterpret_cast<char*>(destination.data()), size);
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
			interrupt, // destination register is actually the interupt number, two source registers are parameters
			jump, // Stores next instruction address in destination, conditional on second source

			move, // One destination, one source
			set, // Source registers byte is the immediate value

			// Aligned, word-addressed (doubles address space); only instructions that allow padding bits (these must be
			// zeroed)
			load, // Only the two source registers (value, address)
			store, // destination, address

			// Three-register ALU ops
			add,
			subtract,
			multiply,
			divide,

			// 4-bit immediate shift size instead of second source (16-bit registers!)
			shift_left,
			shift_right,
			shift_extend,

			// Three-register ALU ops
			logic_and,
			logic_or,
			logic_not
		};

		enum class interrupt : std::uint8_t { halt, extend = 255 };

		struct instruction_word {
			opcode op : 4;
			std::uint8_t destination : 4;
			std::uint8_t source1 : 4;
			std::uint8_t source0 : 4;
		};

		static_assert(sizeof(instruction_word) == 2);

		struct machine_state {
			std::uint16_t program_counter;
			std::array<std::uint16_t, 1 << 4> registers;
			std::vector<std::uint16_t> memory;

			machine_state() : program_counter {}, registers {}, memory(1 << 16) {}
		};

		void dump(const machine_state& state, bool enable_single_step)
		{
			if (!enable_single_step)
				return;

			const auto& [pc, regs, memory] = state;
			std::cout << std::format("pc = {:#06x} ({:#06x})\n", pc, memory[pc]);
			auto i = 0u;
			for (const auto reg : regs) {
				std::cout << std::format("r{} = {:#06x} ({:#06x})\n", i, regs[i], memory[regs[i]]);
				++i;
			}

			std::cin.get();
		}

		void execute(machine_state& state, bool enable_single_step)
		{
			auto halt = false;
			auto& [pc, regs, memory] = state;

			dump(state, enable_single_step);
			while (!halt) {
				instruction_word instr {};
				std::memcpy(&instr, &memory[pc++], 2);
				const auto [op, dst, src1, src0] = instr;
				switch (op) {
				case opcode::interrupt:
					switch (static_cast<interrupt>(dst)) {
					case interrupt::halt:
						halt = true;
						break;

					default:
						break;
					}

					break;

				case opcode::jump:
					if (regs[src1]) {
						regs[dst] = pc + 1;
						pc += regs[src0];
					}

					break;

				case opcode::move:
					regs[dst] = regs[src0];
					break;

				case opcode::set:
					regs[dst] = src1 << 4 | src0;
					break;

				case opcode::load:
					regs[dst] = memory[regs[src0]];
					break;

				case opcode::store:
					memory[regs[src0]] = regs[src1];
					break;

				case opcode::add:
					regs[dst] = regs[src0] + regs[src1];
					break;

				case opcode::subtract:
					regs[dst] = regs[src0] - regs[src1];
					break;

				case opcode::multiply:
					regs[dst] = regs[src0] * regs[src1];
					break;

				case opcode::divide:
					regs[dst] = regs[src1] ? regs[src0] / regs[src1] : 0xffff;
					break;

				case opcode::shift_left:
					regs[dst] = regs[src0] << src1;
					break;

				case opcode::shift_right:
					regs[dst] = regs[src0] >> src1;
					break;

				case opcode::shift_extend:
					regs[dst] = static_cast<std::int16_t>(regs[src0]) >> src1;
					break;

				case opcode::logic_and:
					regs[dst] = regs[src0] & regs[src1];
					break;

				case opcode::logic_or:
					regs[dst] = regs[src0] | regs[src1];
					break;

				case opcode::logic_not:
					regs[dst] = ~regs[src0];
					break;
				}

				dump(state, enable_single_step);
			}
		}
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
	if (!try_read_file(arguments[1], gsl::as_writable_bytes(gsl::span {state.memory}))) {
		std::cerr << "Couldn't load image file '" << arguments[1] << "'\n";
		return 1;
	}

	execute(state, true);
}
