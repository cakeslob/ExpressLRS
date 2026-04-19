import argparse

from esp_compress import binary_compress


def main():
    parser = argparse.ArgumentParser(description="Compress a firmware binary to gzip format.")
    parser.add_argument("input_file", help="Path to the input binary file")
    parser.add_argument("output_file", nargs="?", help="Optional output file path")
    args = parser.parse_args()

    output_file = args.output_file if args.output_file else args.input_file + ".gz"
    binary_compress(output_file, args.input_file)


if __name__ == "__main__":
    main()
