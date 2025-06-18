#!/bin/bash

trials=5
# 128b 1Gb 2Gb
n_records=($((128)) $((1*1024*1024)) )
# 64b 1Kb 1Mb
max_payloads=($((1024)) $((1024*1024)) )

num_threads=(1 2 4 8 16 32)

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
echo "iteration,num_threads,max_payload_size,records_number,time(ms)" >> "$output_file"

for i in $(seq 1 $trials); do
    for mp in "${max_payloads[@]}"; do
        echo "recompiling"
        make cleanall
        make ff_singlenode RPAYLOAD_MAX=$mp
        for nr in "${n_records[@]}"; do
            for nt in "${num_threads[@]}"; do
                output=$(./ff_singlenode -i test_files/file_mp${mp}_nr${nr}.pms -t ${nt} -v 0)
                echo "iteration=$i max_payload=$mp records_number=$nr"
                echo "$output"
                time_ms=$(echo "$output" | grep 'time(ms):' | awk -F ':' '{print $2}')
                echo "$i,$nt,$mp,$nr,$time_ms" >> "$output_file"
            done
        done
    done
    echo "iteration terminated"
done

echo "experiments done!"