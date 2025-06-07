CXX        = g++
MPICXX     = mpicxx 

CXXFLAGS= -std=c++20

INCLUDES   = -Iinclude/fastflow/ -Isrc/
LDFLAGS    = -pthread -Wall -Wextra
OPTFLAGS   = -O3 -ffast-math -DNDEBUG

RPAYLOAD ?= 32

# Targets
TARGETS    = ms_sequential ff_singlenode openmp_singlenode mpi_multinode

# Sources
CORE_SRC   = src/core/core.cpp
SEQ_SRC    = src/sequential/ms_sequential.cpp
UTILS_SRC    = src/utils/utils.cpp

PAR_SRC    = mergeSortPar.cpp
MPI_SRC    = mergeSortDist.cpp

# Object files
CORE_OBJ = $(CORE_SRC:.cpp=.o)
UTILS_OBJ = $(UTILS_SRC:.cpp=.o)
SEQ_OBJ = $(SEQ_SRC:.cpp=.o)

.PHONY: all clean cleanall
.SUFFIXES: .cpp

# Rules
all: $(TARGETS)

ms_sequential: $(SEQ_OBJ) $(CORE_OBJ) $(UTILS_OBJ)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(OPTFLAGS) -o $@ $^ $(LDFLAGS)


mergeSortSeq: $(SEQ_OBJ) $(COMMON_OBJ)
	$(CXX) $(CXXFLAGS) -DRPAYLOAD=$(RPAYLOAD) $(INCLUDES) $(OPTFLAGS) -o $@ $^ $(LDFLAGS)

mergeSortPar: $(PAR_OBJ) $(COMMON_OBJ)
	$(CXX) $(CXXFLAGS) -DRPAYLOAD=$(RPAYLOAD) $(INCLUDES) $(OPTFLAGS) -o $@ $^ $(LDFLAGS)

mergeSortDist: $(MPI_OBJ) $(COMMON_OBJ)
	$(MPICXX) $(CXXFLAGS) -DRPAYLOAD=$(RPAYLOAD) $(INCLUDES) $(OPTFLAGS) -o $@ $^ $(LDFLAGS) -lmpi

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -DRPAYLOAD=$(RPAYLOAD) $(INCLUDES) $(OPTFLAGS) -c $< -o $@
mergeSortDist.o: mergeSortDist.cpp
	$(MPICXX) $(CXXFLAGS) -DRPAYLOAD=$(RPAYLOAD) $(INCLUDES) $(OPTFLAGS) -c $< -o $@

clean:
	rm -f $(TARGETS)

cleanall: clean
	find . -name '*.o' -delete
