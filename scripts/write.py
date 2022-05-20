import struct
import sys


def main():
    if len(sys.argv) != 3:
        print('Usage: write.py <source-file> <output-file>')
        return

    with open(sys.argv[1], 'r') as source:
        with open(sys.argv[2], 'wb') as bin:
            for line in source.readlines():
                for word in line.split():
                    bin.write(struct.pack('B', int(word, base=16)))


if __name__ == '__main__':
    main()
