#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <vector>

#include <gsl/gsl>

#include "constants.h"

namespace bedrock {
	namespace {
		enum class opcode : std::uint8_t {
			jmp,
			mov,
			set,
			lod,
			sto,
			add,
			sub,
			mul,
			div,
			shl,
			shr,
			And,
			lor,
			Not,
			srl,
			dsk
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

			machine_state(const std::filesystem::path& disk_file) :
				pc {},
				regs {},
				memory(1 << 16),
				disk {disk_file, disk.binary | disk.in | disk.out}
			{
				disk.exceptions(disk.badbit | disk.failbit);
			}
		};

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
				case opcode::jmp:
					if (regs[src1]) {
						const auto old_pc = pc;
						pc = regs[src0];
						regs[dst] = old_pc;
					}

					break;

				case opcode::mov:
					regs[dst] = regs[src0];
					break;

				case opcode::set:
					regs[dst] = src1 << 4 | src0;
					break;

				case opcode::lod:
					regs[dst] = memory[regs[src0]];
					break;

				case opcode::sto:
					memory[regs[src0]] = regs[src1];
					break;

				case opcode::add:
					regs[dst] = regs[src0] + regs[src1];
					break;

				case opcode::sub:
					regs[dst] = regs[src0] - regs[src1];
					break;

				case opcode::mul:
					regs[dst] = regs[src0] * regs[src1];
					break;

				case opcode::div:
					regs[dst] = regs[src1] ? regs[src0] / regs[src1] : 0xffff;
					break;

				case opcode::shl:
					regs[dst] = regs[src0] << src1;
					break;

				case opcode::shr:
					regs[dst] = regs[src0] >> src1;
					break;

				case opcode::And:
					regs[dst] = regs[src0] & regs[src1];
					break;

				case opcode::lor:
					regs[dst] = regs[src0] | regs[src1];
					break;

				case opcode::Not:
					regs[dst] = ~regs[src0];
					break;

				case opcode::srl: {
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

				case opcode::dsk: {
					const auto dst_address = regs[dst];
					// Why not 511? Because RAM is word-addressed
					if (dst_address & (words_per_sector - 1)) {
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
