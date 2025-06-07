#!/bin/bash

set -euo pipefail

OUTFILE="${1:-}"
NUM_RECORDS="${2:-}"
MAX_PAYLOAD="${3:-}"

if [[ -z "$OUTFILE" || -z "$NUM_RECORDS" || -z "$MAX_PAYLOAD" ]]; then
    echo "Usage: $0 output_file num_records max_payload_size"
    exit 1
fi

: > "$OUTFILE"

reverse_bytes() {
    fold -w2 | tac | tr -d '\n'
}

write_uint64_le() {
    local val="$1"
    hex=$(printf "%016x" "$val")
    rev_hex=$(echo "$hex" | reverse_bytes)
    echo -n "$rev_hex" | xxd -r -p >> "$OUTFILE"
}

write_uint32_le() {
    local val="$1"
    hex=$(printf "%08x" "$val")
    rev_hex=$(echo "$hex" | reverse_bytes)
    echo -n "$rev_hex" | xxd -r -p >> "$OUTFILE"
}

write_uint64_le "$MAX_PAYLOAD"
write_uint64_le "$NUM_RECORDS"

for ((i=0; i<NUM_RECORDS; i++)); do
    KEY_HIGH=$(od -An -N4 -tu4 < /dev/urandom | tr -d ' ')
    KEY_LOW=$(od -An -N4 -tu4 < /dev/urandom | tr -d ' ')
    KEY=$(( (KEY_HIGH << 32) | KEY_LOW ))

    write_uint64_le "$KEY"

    PAYLOAD_LEN=$(( (RANDOM % MAX_PAYLOAD) + 1 ))
    write_uint32_le "$PAYLOAD_LEN"

    head -c "$PAYLOAD_LEN" /dev/urandom >> "$OUTFILE"

    PADDING_LEN=$(( MAX_PAYLOAD - PAYLOAD_LEN ))
    if (( PADDING_LEN > 0 )); then
        head -c "$PADDING_LEN" < /dev/zero >> "$OUTFILE"
    fi
done

echo "✅ Generated $NUM_RECORDS records in '$OUTFILE' with max payload size $MAX_PAYLOAD"
