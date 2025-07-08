#!/bin/bash

trials=5
# 128b 1Gb 2Gb
n_records=($((128)) $((1*1024*1024)) $((2*1024*1024)) $((3*1024*1024))  )
# 64b 1Kb 1Mb
max_payloads=( $((128)) $((1024)) )

num_nodes=(1 2 4 8)

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
        make mpi_multinode RPAYLOAD_MAX=$mp
        for nr in "${n_records[@]}"; do
            for nt in "${num_threads[@]}"; do
                for nn in "${num_nodes[@]}"; do
                    echo "iteration=$i max_payload=$mp records_number=$nr  num_threads=$nt num_nodes=$nn"
                    output=$(mpirun --bind-to none -N $nn ./mpi_multinode -i test_files/file_mp${mp}_nr${nr}.pms -o test_files/file_mp${mp}_nr${nr}_out.pms -t ${nt} -v 0)
                    echo "$output"
                    time_ms=$(echo "$output" | grep 'time(ms):' | awk -F ':' '{print $2}')
                    echo "$i,$nt,$mp,$nr,$time_ms" >> "$output_file"
                    rm test_files/file_mp${mp}_nr${nr}_out.pms
                done

            done
        done
    done
    echo "iteration terminated"
done

echo "experiments done!"