import struct
import sys


def main():
    if len(sys.argv) != 2:
        print('Usage: write.py <source-file>')
        return

    with open(sys.argv[1], 'r') as source:
        content = ''.join(source.readlines())
        data = bytes(content, 'utf-8')
        if len(data) % 2:
            data += b'\0'

        for i, b in enumerate(data):
            if i % 2:
                print(f'{("0" if b < 0x10 else "") + hex(b).split("x")[1].upper()},')
            else:
                print(f'0x{("0" if b < 0x10 else "") + hex(b).split("x")[1].upper()}', end='')


if __name__ == '__main__':
    main()
