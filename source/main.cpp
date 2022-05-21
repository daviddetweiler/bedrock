#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <vector>

#include <gsl/gsl>

namespace bedrock {
	namespace {
		enum class opcode : std::uint8_t {
			jump,
			move,
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
			serial,
			disk
		};

		struct instruction_word {
			opcode op;
			std::uint8_t dst;
			std::uint8_t src1;
			std::uint8_t src0;
		};

		struct machine_state {
			std::uint16_t pc;
			std::array<std::uint16_t, 1 << 4> regs;
			std::vector<std::uint16_t> memory;
			std::fstream disk;

			machine_state(const std::filesystem::path& disk_file) : pc {}, regs {}, memory(1 << 16), disk {}
			{
				disk.exceptions(disk.badbit | disk.failbit);
				disk.open(disk_file, disk.binary | disk.in | disk.out);
			}
		};

		constexpr auto word_size = sizeof(std::uint16_t);
		constexpr auto sector_size = 512;
		constexpr auto disk_size = sector_size * (1 << 16);

		instruction_word decode(std::uint16_t word) noexcept
		{
			const auto op = (word & 0xf000) >> 12;
			const auto dst = (word & 0x0f00) >> 8;
			const auto src1 = (word & 0x00f0) >> 4;
			const auto src0 = word & 0x000f;
			return {
				static_cast<opcode>(op),
				gsl::narrow_cast<std::uint8_t>(dst),
				gsl::narrow_cast<std::uint8_t>(src1),
				gsl::narrow_cast<std::uint8_t>(src0)};
		}

		void execute(machine_state& state)
		{
			auto halt = false;
			auto& [pc, regs, memory, disk] = state;
			while (!halt) {
				const auto [op, dst, src1, src0] = decode(memory[pc++]);
				switch (op) {
				case opcode::jump:
					if (regs[src1]) {
						const auto old_pc = pc;
						pc = regs[src0];
						regs[dst] = old_pc;
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

				case opcode::logic_and:
					regs[dst] = regs[src0] & regs[src1];
					break;

				case opcode::logic_or:
					regs[dst] = regs[src0] | regs[src1];
					break;

				case opcode::logic_not:
					regs[dst] = ~regs[src0];
					break;

				case opcode::serial: {
					const auto is_other_port = src1 & 0b10;
					const auto is_write = src1 & 0b1;
					if (!is_other_port) {
						if (is_write)
							std::cout.put(regs[src0]);
						else
							regs[dst] = std::cin.get();
					}
					else {
						// Until we actually support COM2, we pretend it's connected to a zero emitter
						if (!is_write)
							regs[dst] = 0;
					}

					break;
				}

				case opcode::disk: {
					const auto dst_address = regs[dst];
					// Why not 511? Because RAM is word-addressed
					if (dst_address & ((sector_size / word_size) - 1)) {
						halt = true;
						break;
					}

					disk.seekg(regs[src0] * sector_size);
					const auto is_write = src1 & 0b1;
					if (is_write)
						disk.write(reinterpret_cast<char*>(&memory[dst_address]), sector_size);
					else
						disk.read(reinterpret_cast<char*>(&memory[dst_address]), sector_size);

					break;
				}
				}
			}
		}

		constexpr std::array<std::uint16_t, sector_size / word_size> boot_sector {
			0x2F58, // set	rf, 0x58 	; rf = 'X'

			// Wait for input, stash it
			0xE000, // srl 	r0, 0x0, r0
			0x1200, // mov	r2, r0

			// Compare to X
			0xD10F, // not	r1, rf
			0xB101, // and	r1, r0, r1
			0xD000, // not	r0, r0
			0xB00F, // and	r0, r0, rf
			0xC001, // or	r0, r0, r1	; r0 is zero if char == 'X'

			// If char did not equal X, skip execute jump
			0x210D, // set	r1, 0xd
			0x0001, // jmp	r0, r0, r1

			// Jump to code buffer
			0x2101, // set	r1, 0x1
			0x9181, // shl	r1, 0x8, r1
			0x0011, // jmp	r0, r1, r1	; if r1 jump to r1

			// Decide range of character
			0x203A, // set	r0, 0x3a	; r0 = ':'
			0x8002, // div	r0, r0, r2	; r0 = r2 / r0 (zero iff. r2 < ':')

			// Jump if not decimal to letter computation
			0x2115, // set	r1, 0x15
			0x0101, // jmp	r1, r0, r1	; if r0 goto r1

			// Compute decimal and skip letter computation
			0x2030, // set	r0, 0x30	; r0 = '0'
			0x6002, // sub	r0, r0, r2	; r0 = r2 - r0
			0x2117, // set	r1, 0x17
			0x0111, // jmp	r1, r1, r1

			// Compute letter
			0x2037, // set	r0, 0x37	; r0 = 'A' - 10
			0x6002, // sub	r0, r0, r2	; r0 = r2 - r0

			// Shift letter in
			0x9E4E, // shl	re, 0x4, re
			0xCE0E, // or	re,	r0, re

			// Change state
			0x2001, // set	r0, 0x1
			0x5DD0, // add	rd, rd, r0
			0x2003, // set	r0, 0x3
			0xB00D, // and	r0, r0, rd

			// Skip write while not needed
			0x2124, // set	r1, 0x24
			0x0101, // jmp	r1, r0, r1	; if r0 goto r1

			// Write!
			0x2101, // set	r1, 0x1
			0x9081, // shl	r0, 0x8, r1
			0x500C, // add	r0, r0, rc
			0x40E0, // sto	re, r0
			0x5C1C, // add	rc, r1, rc

			// Loop!
			0x2001, // set	r0, 0x1
			0x0000, // jmp	r0, r0, r0
		};
	}
}

using namespace bedrock;

int main(int argc, char** argv)
{
	const gsl::span arguments {argv, gsl::narrow<std::size_t>(argc)};
	if (argc != 2) {
		std::cerr << "Usage: vm <disk>\n";
		return 1;
	}

	const std::filesystem::path path {arguments[1]};
	if (!std::filesystem::exists(path)) {
		std::ofstream file {path, file.binary};
		file.exceptions(file.badbit | file.failbit);
		file.write(reinterpret_cast<const char*>(boot_sector.data()), boot_sector.size() * word_size);
		file.seekp(disk_size - 1);
		file.put('\0');
	}

	if (std::filesystem::file_size(path) != disk_size) {
		std::cerr << "Disk must be " << disk_size << " bytes\n";
		return 1;
	}

	machine_state state {path};
	state.disk.read(reinterpret_cast<char*>(&state.memory[0]), sector_size);
	std::cout << "\x1b[2J\x1b[H";
	execute(state);
	std::cout << "\x1b[2J\x1b[H";
}
