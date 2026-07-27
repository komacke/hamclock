#!/usr/bin/env bash

set -euo pipefail

# --- argument validation ---
if [[ $# -ne 3 ]]; then
	echo "insert license file" >&2
	echo "Usage: $0 <input_file> <insert_file> <output_file>" >&2
	exit 1
fi

INPUT_FILE="$1"
INSERT_FILE="$2"
OUTPUT_FILE="$3"

if [[ ! -f "$INPUT_FILE" ]]; then
	echo "insert license file" >&2
	echo "Error: input file '$INPUT_FILE' not found." >&2
	exit 1
fi

if [[ ! -f "$INSERT_FILE" ]]; then
	echo "insert license file" >&2
	echo "Error: insert file '$INSERT_FILE' not found." >&2
	exit 1
fi

# --- replacement ---

awk -v insert_file="$INSERT_FILE" '
	/LICENSE_BEGIN/ {
		print          # keep the LICENSE_START line
		# emit the insert file contents
		while ((getline line < insert_file) > 0)
			print line
		close(insert_file)
		skip = 1
		next
	}
	/LICENSE_END/ {
		skip = 0
		print          # keep the LICENSE_END line
		next
	}
	skip { next }      # drop lines between the markers
	{ print }
' "$INPUT_FILE" > "$OUTPUT_FILE"

echo "insert license file" 
echo "Done. Generated $OUTPUT_FILE"
