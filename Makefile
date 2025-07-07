CXX        = g++
MPICXX     = mpicxx 

CXXFLAGS= -std=c++20 -fopenmp

INCLUDES   = -Iinclude/fastflow/ -Isrc/
LDFLAGS    = -pthread -Wall -Wextra
OPTFLAGS   = -ffast-math -O3 -ftree-vectorize -DNDEBUG

RPAYLOAD_MAX = 32

# Targets
TARGETS    = ms_sequential ff_singlenode openmp_singlenode mpi_multinode payload_generator

# Sources
CORE_SRC   = src/core/core.cpp
SEQ_SRC    = src/sequential/ms_sequential.cpp
FF_SRC 	   = src/ff_singlenode/ms_ff_singlenode.cpp
OMP_SRC    = src/openmp_singlenode/ms_openmp_singlenode.cpp
MPI_SRC    = src/multinode/ms_multinode.cpp
UTILS_SRC  = src/utils/utils.cpp
# Object files
CORE_OBJ = $(CORE_SRC:.cpp=.o)
FF_OBJ = $(FF_SRC:.cpp=.o)
OMP_OBJ = $(OMP_SRC:.cpp=.o)
MPI_OBJ = $(MPI_SRC:.cpp=.o)
UTILS_OBJ = $(UTILS_SRC:.cpp=.o)
SEQ_OBJ = $(SEQ_SRC:.cpp=.o)

.PHONY: all clean cleanall
.SUFFIXES: .cpp

# Rules
all: $(TARGETS)

ms_sequential: $(SEQ_OBJ) $(CORE_OBJ) $(UTILS_OBJ)
	$(CXX) $(CXXFLAGS) -DRPAYLOAD_MAX=$(RPAYLOAD_MAX) $(INCLUDES) $(OPTFLAGS) -o $@ $^ $(LDFLAGS)

ff_singlenode: $(FF_OBJ) $(CORE_OBJ) $(UTILS_OBJ)
	$(CXX) $(CXXFLAGS) -DRPAYLOAD_MAX=$(RPAYLOAD_MAX) $(INCLUDES) $(OPTFLAGS) -o $@ $^ $(LDFLAGS)

openmp_singlenode: $(OMP_OBJ) $(CORE_OBJ) $(UTILS_OBJ)
	$(CXX) $(CXXFLAGS) -DRPAYLOAD_MAX=$(RPAYLOAD_MAX) $(INCLUDES) $(OPTFLAGS) -o $@ $^ -fopenmp $(LDFLAGS) 

mpi_multinode: $(MPI_OBJ) $(CORE_OBJ) $(UTILS_OBJ)
	$(MPICXX) $(CXXFLAGS) -DRPAYLOAD_MAX=$(RPAYLOAD_MAX) $(INCLUDES) $(OPTFLAGS) -o $@ $^ -fopenmp $(LDFLAGS) 
payload_generator:utilities/payload_generator.cpp 
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(OPTFLAGS) -o utilities/$@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -DRPAYLOAD_MAX=$(RPAYLOAD_MAX)  $(INCLUDES) $(OPTFLAGS) -c $< -o $@

clean:
	rm -f $(TARGETS)

cleanall: clean
	find . -name '*.o' -delete
