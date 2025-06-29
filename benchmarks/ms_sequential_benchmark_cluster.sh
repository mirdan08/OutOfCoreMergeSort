#!/bin/bash

trials=5
# 128b 1Gb 2Gb
n_records=($((1*1024*1024)) $((2*1024*1024)) )
# 1kb 10kb 10Mb
max_payloads=($((1024)) $((10*1024)) $((1024*1024))  )

make cleanall
make payload_generator
if ! [ -f "test_files" ]; then
    mkdir -p test_files
fi

for mp in "${max_payloads[@]}"; do
    for nr in "${n_records[@]}"; do
    if ! [ -f "test_files/file_mp${mp}_nr${nr}.pms" ]; then
        ./utilities/payload_generator -o test_files/file_mp${mp}_nr${nr}.pms -p $mp -r $nr -v 0
    fi
    echo "test_files/file_mp${mp}_nr${nr}.pms already exists"
    done
done

echo "files generated!"
echo "starting experiments:"

output_file="$1"
touch "$output_file"
echo "" > "$output_file"
echo "iteration,max_payload_size,records_number,time(ms)" >> "$output_file"

for i in $(seq 1 $trials); do
    for mp in "${max_payloads[@]}"; do
        for nr in "${n_records[@]}"; do
            make cleanall
            make RPAYLOAD_MAX=$mp ms_sequential
            output=$(srun --time=00:5:00 ./ms_sequential -i test_files/file_mp${mp}_nr${nr}.pms -o test_files/file_mp${mp}_nr${nr}_out.pms  -v 0)
            echo "iteration=$i max_payload=$mp records_number=$nr"
            echo "$output"
            time_ms=$(echo "$output" | grep 'time(ms):' | awk -F ':' '{print $2}')
            echo "$i,$mp,$nr,$time_ms" >> "$output_file"
            rm test_files/file_mp${mp}_nr${nr}_out.pms
        done
    done
done

echo "experiments done!"