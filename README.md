# Out-of-core mergesort

This is the implementation of the mergesort algorithm in an out-of-core manner using FastFlow,OpenMP and  MPI.

## Project files organization

The source for all the implementations is contained in the `src` directory:
- `core` contains the core classes used in all the implementations,specifically
    - `sequential` contains `ms_sequential.cpp` which is the sequential version.
    - `ff_singlenode` contains `ms_ff_singlenode.cpp` which is the single-node version done with FastFlow. The file contains allso the FastFlow's building blocks used in the implementation.
    - `openmp_singlenode` contains `ms_openmp_singlenode.cpp` which is the single-node version done with OpenMP.
    - `multinode` contains `ms_multinode.cpp` which is the single-node version done with OpenMP+MPI.

Additionally we have:
- The file `utilities/payload_generator.cpp` which which hold the utility to generate the files used in the tests and check their sorting/validate the data.
- The folder `benchmarks` which hold the bash files used to benchmark/debug the various implementations
    - the files with suffix `_cluster.sh` are used to do the tests on the cluster the other ones are used for local debugging.

## Compiling the project

In the Makefile we have the targets for each version:
- `ms_sequential` for the sequential version.
- `ff_singlnode` for FastFlow's single-node version.
- `openmp_singlenode` for OpenMP's single-node version.
- `mpi_multinode` for the OpenMP+MPI multi-node version.

The additional `payload_generator` target will generate the corresponding payload generator utility.

## Running the project

All versions use the same arguments, this is the full command:
- `<your version> -t <number of threads> -v (1 if you want to see want to plot values after sorting 0 if not) -i <input file name> -o <output file name> -m <maximum buffer memory limit in bytes>`

All parameters are mandatory except for the -t parameter in the sequential version which  is not being used.

If you want to use the payload generator the arguments are:
- `payload_generator -p <max payload size in bytes> -r <number of records> -o <output file name> -d`
For this the -d flag is used to check if the output file is sorted and has len values above 8.

## running the benchmarks

For the benchmarks you can just run the bash scripts in the `benchmark` directory corresponding to the version you want to try, each benchmark will take as a first argument a file where it will store the results and will display all outputs from the current experiment in the console.

