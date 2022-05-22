#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

#include <gsl/gsl>

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
			bsr,
			bsw
		};

		struct instruction_word {
			opcode op;
			std::uint8_t dst;
			std::uint8_t src1;
			std::uint8_t src0;
		};

		struct disk_controller {
			std::fstream file;
			std::uint16_t sector_count;
			std::uint16_t sector;
			std::uint16_t address;
		};

		struct machine_state {
			std::uint16_t pc;
			std::array<std::uint16_t, 1 << 4> regs;
			std::vector<std::uint16_t> memory;

			disk_controller disk0;
			disk_controller disk1;
			bool halt;

			machine_state() : pc {}, regs {}, memory(1 << 16), disk0 {}, disk1 {}, halt {false} {}
		};

		constexpr auto word_size = sizeof(std::uint16_t);
		constexpr auto sector_size = 512;
		constexpr auto sector_words = sector_size / word_size;
		constexpr auto disk_size = sector_size * (1 << 16);

		void do_disk_operation(disk_controller& disk, std::uint16_t control)
		{
			if (!disk.file.is_open())
				return;

			switch (control) {
			case 1: // read
				break;

			case 2: // write
				break;

			default:
				break;
			}

			std::terminate();
		}

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

		void do_jmp(machine_state& state, std::uint8_t dst, std::uint8_t src1, std::uint8_t src0)
		{
			if (state.regs[src1]) {
				const auto old_pc = state.pc;
				state.pc = state.regs[src0];
				state.regs[dst] = old_pc;
			}
		}

		void do_bsr(machine_state& state, std::uint8_t dst, std::uint8_t src1, std::uint8_t src0)
		{
			const auto port = state.regs[src0];
			switch (port) {
			case 0x0000:
				state.regs[dst] = std::cin.get();
				break;

			case 0x0002:
				state.regs[dst] = state.disk0.sector_count;
				break;

			case 0x0003:
				state.regs[dst] = state.disk0.sector;
				break;

			case 0x0004:
				state.regs[dst] = state.disk0.address;
				break;

			case 0x0005:
				state.regs[dst] = state.disk1.sector_count;
				break;

			case 0x0006:
				state.regs[dst] = state.disk1.sector;
				break;

			case 0x0007:
				state.regs[dst] = state.disk1.address;
				break;

			default:
				state.regs[dst] = 0;
			}
		}

		void do_bsw(machine_state& state, std::uint8_t dst, std::uint8_t src1, std::uint8_t src0)
		{
			const auto port = state.regs[src0];
			const auto word = state.regs[src1];
			switch (port) {
			case 0x0000:
				std::cout.put(word);
				break;

			case 0x0001:
				state.halt = word;
				break;

			case 0x0002:
				do_disk_operation(state.disk0, word);
				break;

			case 0x0003:
				state.disk0.sector = word;
				break;

			case 0x0004:
				state.disk0.address = word;
				break;

			case 0x0005:
				do_disk_operation(state.disk1, word);
				break;

			case 0x0006:
				state.disk1.sector = word;
				break;

			case 0x0007:
				state.disk1.address = word;
				break;

			default:
				break;
			}
		}

		void execute(machine_state& state)
		{
			auto& [pc, regs, memory, disk0, disk1, halt] = state;
			while (!halt) {
				const auto [op, dst, src1, src0] = decode(memory[pc++]);
				switch (op) {
				case opcode::jmp:
					do_jmp(state, dst, src1, src0);
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

				case opcode::bsr:
					do_bsr(state, dst, src1, src0);
					break;

				case opcode::bsw:
					do_bsw(state, dst, src1, src0);
					break;
				}
			}
		}

		/*
			rc, rd, re, and rf are used by the assembler for persistent state. They should be reinitialized to zero
			before re-entering the assembler loop.
			What follows is a minimal "return sequence":

			2c00
			2d00
			2e00
			2f00
			2001
			000f
		*/

		constexpr std::array<std::uint16_t, 32> assembler {
			// Wait for input
			0xE20C, // bsr 	r2, rc

			// If char did not equal '\n', skip execute jump
			0x210A, // set	r1, 0xa
			0x6021, // sub	r0, r2, r1	; r0 is zero if char == '\n'
			0x2107, // set	r1, 0x7
			0x0001, // jmp	r0, r0, r1

			// Jump to code buffer
			0x2140, // set	r1, 0x40
			0x0011, // jmp	r0, r1, r1	; if r1 jump to r1

			// Decide range of character
			0x203A, // set	r0, 0x3a	; r0 = ':'
			0x8002, // div	r0, r0, r2	; r0 = r2 / r0 (zero iff. r2 < ':')

			// Jump if not decimal to letter computation
			0x210F, // set	r1, 0xf
			0x0101, // jmp	r1, r0, r1	; if r0 goto r1

			// Compute decimal and skip letter computation
			0x2030, // set	r0, 0x30	; r0 = '0'
			0x6002, // sub	r0, r0, r2	; r0 = r2 - r0
			0x2111, // set	r1, 0x11
			0x0111, // jmp	r1, r1, r1

			// Compute letter
			0x2057, // set	r0, 0x57	; r0 = 'a' - 10
			0x6002, // sub	r0, r0, r2	; r0 = r2 - r0

			// Shift letter in
			0x9F4F, // shl	rf, 0x4, rf
			0xCF0F, // lor	rf,	r0, rf

			// Change state
			0x2201, // set	r2, 0x1
			0x5EE2, // add	re, re, r2
			0x2003, // set	r0, 0x3
			0xB00E, // and	r0, r0, re

			// Skip write while not needed
			0x211E, // set	r1, 0x1e
			0x0101, // jmp	r1, r0, r1	; if r0 goto r1

			// Write!
			0x2040, // set	r0, 0x40
			0x500D, // add	r0, r0, rd
			0x40F0, // sto	rf, r0
			0x5D2D, // add	rd, r2, rd

			// Dispose of trailing newline
			0xE003, // bsr 	r0, r3

			// Loop!
			0x2100, // set	r1, 0x0
			0x0001, // jmp	r0, r0, r1
		};
	}
}

using namespace bedrock;

int main()
{
	machine_state state {};
	std::copy(assembler.begin(), assembler.end(), state.memory.begin());
	std::cout << "\x1b[2J\x1b[H";
	execute(state);
}
