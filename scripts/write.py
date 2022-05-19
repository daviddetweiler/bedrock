import struct
import sys


def main():
    if len(sys.argv) != 3:
        print('Usage: write.py <source-file> <output-file>')
        return

    with open(sys.argv[1], 'r') as source:
        with open(sys.argv[2], 'wb') as bin:
            for line in source.readlines():
                line = line.strip()
                if not len(line):
                    continue

                value = int(line, 16)
                bin.write(struct.pack('>H', value))


if __name__ == '__main__':
    main()
