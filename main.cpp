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
		constexpr auto word_size = sizeof(std::uint16_t);
		constexpr auto sector_size = 512;
		constexpr auto sector_words = sector_size / word_size;
		constexpr auto disk_size = sector_size * (1 << 16);
		constexpr auto max_word = std::numeric_limits<std::uint16_t>::max();

		enum class opcode : std::uint8_t {
			jmp,
			rhi,
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

			disk_controller(const char* path) : file {}, sector_count {}, sector {}, address {}
			{
				if (path) {
					file.exceptions(file.badbit | file.failbit);
					file.open(path, file.binary | file.ate | file.in | file.out);
					const auto n_sectors = file.tellg() / sector_size;
					sector_count = n_sectors < max_word ? static_cast<std::uint16_t>(n_sectors) : max_word;
				}
			}
		};

		constexpr std::array<std::uint16_t, 40> assembler {
			// Detect size of disk0
			0x2001, // set 	r0, 0x1
			0xEB00, // bsr 	rb, r0

			// Set assembly area base address to after assembler
			0x2B28, // set 	rb, 0x28

			// Jump to boot shim if disk0 is present (non-zero size)
			0x2108, // set 	r1, 0x8
			0x0201, // jmp 	r2, r0, r1

			// disk0 not present, jump to assembler
			0x210A, // set	r1, 0xa
			0x0211, // jmp	r2, r1, r1
			0xC000, // lor	r0, r0, r0	; nop

			// Read disk0 sector zero over ourselves, jump to after assembler
			0xF0C0, // bsw	rc, r0
			0x00BB, // jmp	r0, rb, rb

			// Wait for input
			0xE20C, // bsr 	r2, rc

			// If char did not equal '\n', skip execute jump
			0x210A, // set	r1, 0xa
			0x6021, // sub	r0, r2, r1	; r0 is zero if char == '\n'
			0x2110, // set	r1, 0x10
			0x0001, // jmp	r0, r0, r1

			// Jump to code buffer
			0x00BB, // jmp	r0, rb, rb	; if r1 jump to r1

			// Decide range of character
			0x203A, // set	r0, 0x3a	; r0 = ':'
			0x8002, // div	r0, r0, r2	; r0 = r2 / r0 (zero iff. r2 < ':')

			// Jump if not decimal to letter computation
			0x2118, // set	r1, 0x18
			0x0101, // jmp	r1, r0, r1	; if r0 goto r1

			// Compute decimal and skip letter computation
			0x2030, // set	r0, 0x30	; r0 = '0'
			0x6002, // sub	r0, r0, r2	; r0 = r2 - r0
			0x211A, // set	r1, 0x1a
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
			0x2126, // set	r1, 0x26
			0x0101, // jmp	r1, r0, r1	; if r0 goto r1

			// Write!
			0x50BD, // add	r0, rb, rd
			0x40F0, // sto	rf, r0
			0x5D2D, // add	rd, r2, rd

			// Dispose of trailing newline
			0xE00C, // bsr 	r0, rc

			// Loop!
			0x210A, // set	r1, 0xa
			0x0001, // jmp	r0, r0, r1
		};

		class memory_adapter {
		public:
			memory_adapter() : memory((1 << 16) - assembler.size()) {}

			void write(std::uint16_t address, std::uint16_t word)
			{
				if (address >= assembler.size())
					memory[address - assembler.size()] = word;
			}

			auto read(std::uint16_t address)
			{
				if (address >= assembler.size())
					return memory[address - assembler.size()];
				else
					return assembler[address];
			}

		private:
			std::vector<std::uint16_t> memory;
		};

		struct machine_state {
			std::uint16_t pc;
			std::uint16_t hi;
			std::array<std::uint16_t, 1 << 4> regs;
			memory_adapter memory;
			disk_controller disk0;
			disk_controller disk1;
			bool halt;

			machine_state(const char* disk0_path, const char* disk1_path) :
				pc {},
				hi {},
				regs {},
				memory {},
				disk0 {disk0_path},
				disk1 {disk1_path},
				halt {false}
			{
			}
		};

		void do_disk_operation(disk_controller& disk, memory_adapter& memory, std::uint16_t control)
		{
			if (!disk.file.is_open())
				return;

			switch (control) {
			case 0:
				if (disk.sector < disk.sector_count) {
					disk.file.seekg(sector_size * disk.sector);
					for (auto i = 0u; i < sector_words; ++i)
						memory.write(disk.address + i, disk.file.get() << 8 | disk.file.get());
				}

				break;

			case 1:
				if (disk.sector < disk.sector_count) {
					disk.file.seekg(sector_size * disk.sector);
					for (auto i = 0u; i < sector_size; ++i) {
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

		instruction_word decode(std::uint16_t word) noexcept
		{
			const auto op = (word & 0xf000) >> 12;
			const auto dst = (word & 0x0f00) >> 8;
			const auto src1 = (word & 0x00f0) >> 4;
			const auto src0 = word & 0x000f;
			return {
				static_cast<opcode>(op),
				static_cast<std::uint8_t>(dst),
				static_cast<std::uint8_t>(src1),
				static_cast<std::uint8_t>(src0)};
		}

		void do_jmp(machine_state& state, std::uint8_t dst, std::uint8_t src1, std::uint8_t src0)
		{
			if (state.regs[src1]) {
				const auto old_pc = state.pc;
				state.pc = state.regs[src0];
				state.regs[dst] = old_pc;
			}
		}

		void do_bsr(machine_state& state, std::uint8_t dst, std::uint8_t, std::uint8_t src0)
		{
			const auto port = state.regs[src0];
			switch (port) {
			case 0x0000:
				state.regs[dst] = std::cin.get() & 0xff;
				break;

			case 0x0001:
				state.regs[dst] = state.disk0.sector_count;
				break;

			case 0x0002:
				state.regs[dst] = state.disk0.sector;
				break;

			case 0x0003:
				state.regs[dst] = state.disk0.address;
				break;

			case 0x0004:
				state.regs[dst] = state.disk1.sector_count;
				break;

			case 0x0005:
				state.regs[dst] = state.disk1.sector;
				break;

			case 0x0006:
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
				std::cout.put(word & 0xff);
				break;

			case 0x0001:
				do_disk_operation(state.disk0, state.memory, word);
				break;

			case 0x0002:
				state.disk0.sector = word;
				break;

			case 0x0003:
				state.disk0.address = word;
				break;

			case 0x0004:
				do_disk_operation(state.disk1, state.memory, word);
				break;

			case 0x0005:
				state.disk1.sector = word;
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
			auto& [pc, hi, regs, memory, disk0, disk1, halt] = state;
			while (!halt) {
				const auto [op, dst, src1, src0] = decode(memory.read(pc++));
				switch (op) {
				case opcode::jmp:
					do_jmp(state, dst, src1, src0);
					break;

				case opcode::rhi:
					regs[dst] = hi;
					break;

				case opcode::set:
					regs[dst] = src1 << 4 | src0;
					break;

				case opcode::lod:
					regs[dst] = memory.read(regs[src0]);
					break;

				case opcode::sto:
					memory.write(regs[src0], regs[src1]);
					break;

				case opcode::add: {
					const std::uint32_t a {regs[src0]};
					const std::uint32_t b {regs[src1]};
					const auto c = a + b;
					regs[dst] = c & 0xffff;
					hi = c >> 16;
					break;
				}

				case opcode::sub: {
					const std::uint32_t a {regs[src0]};
					const std::uint32_t b {regs[src1]};
					const auto c = a - b;
					regs[dst] = c & 0xffff;
					hi = c >> 16;
					break;
				}

				case opcode::mul: {
					const std::uint32_t a {regs[src0]};
					const std::uint32_t b {regs[src1]};
					const auto c = a * b;
					regs[dst] = c & 0xffff;
					hi = c >> 16;
					break;
				}

				case opcode::div: {
					const std::uint32_t a {regs[src0]};
					const std::uint32_t b {regs[src1]};
					const std::uint32_t c {b ? a / b : 0xffffffff};
					regs[dst] = c & 0xffff;
					hi = c >> 16;
					break;
				}

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
