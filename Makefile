CPX_BIN_DIR = bin
BIN_DIR = mg-cfd/bin
OBJ_DIR = obj
SRC_DIR = src

ifdef OP2_INSTALL_PATH
  OP2_INC = -I$(OP2_INSTALL_PATH)/c/include
  OP2_LIB = -L$(OP2_INSTALL_PATH)/c/lib
endif

ifdef CUDA_INSTALL_PATH
  CUDA_INC = -I$(CUDA_INSTALL_PATH)/include
  CUDA_LIB = -L$(CUDA_INSTALL_PATH)/lib64
endif

ifdef HDF5_INSTALL_PATH
  HDF5_INC = -I$(HDF5_INSTALL_PATH)/include
  HDF5_LIB = -L$(HDF5_INSTALL_PATH)/lib
endif
HDF5_LIB += -lhdf5 -lz

PARMETIS_VER=4
ifdef PARMETIS_INSTALL_PATH
  PARMETIS_INC = -I$(PARMETIS_INSTALL_PATH)/include
  PARMETIS_LIB = -L$(PARMETIS_INSTALL_PATH)/lib
endif
PARMETIS_INC += -DHAVE_PARMETIS
PARMETIS_LIB += -lparmetis -lmetis
ifeq ($(PARMETIS_VER),4)
  PARMETIS_INC += -DPARMETIS_VER_4
endif

ifdef PTSCOTCH_INSTALL_PATH
  PTSCOTCH_INC 	= -I$(PTSCOTCH_INSTALL_PATH)/include
  PTSCOTCH_LIB 	= -L$(PTSCOTCH_INSTALL_PATH)/lib
endif
PTSCOTCH_INC += -DHAVE_PTSCOTCH
PTSCOTCH_LIB += -lptscotch -lscotch -lptscotcherr

ifdef PETSC_INSTALL_PATH
    PETSC_INC = -I$(PETSC_INSTALL_PATH)/include
    PETSC_LIB = -L$(PETSC_INSTALL_PATH)/lib
endif

ifdef DOLFINX_INSTALL_PATH
    DOLFINX_INC = -I$(DOLFINX_INSTALL_PATH)/include
    DOLFINX_LIB = -L$(DOLFINX_INSTALL_PATH)/lib64
endif

ifdef BOOST_INSTALL_PATH
    BOOST_LIB = -L$(BOOST_INSTALL_PATH)/lib
endif

ifdef SQLITE_INSTALL_PATH
    SQLITE_INC = -I$(SQLITE_INSTALL_PATH)/include
    SQLITE_LIB = -L$(SQLITE_INSTALL_PATH)/lib
endif

ifdef TREETIMER_INSTALL_PATH
    TREETIMER_INC = -I$(TREETIMER_INSTALL_PATH)/include/timing_library/interface
    TREETIMER_LIB = -L$(TREETIMER_INSTALL_PATH) -ltt
endif

ifdef SIMPIC_INSTALL_PATH
    SIMPIC_INC = -I$(SIMPIC_INSTALL_PATH)
    SIMPIC_LIB = -L$(SIMPIC_INSTALL_PATH)/libsimpic.a
endif

ifdef FENICS
    FENICS_DEF = -Ddeffenics
    DOLFINX_LIB += -ldolfinx
    FENICS_LIB = $(BIN_DIR)/libdolfinx_cpx.a
    PETSC_INC += -DHAVE_PETSC
    PETSC_LIB += -lpetsc
    BOOST_LIB += -lboost_filesystem -lboost_program_options -lboost_timer
endif

ifdef MGCFD
    MGCFD_DEF = -Ddefmgcfd
    MG_LIB = $(BIN_DIR)/mgcfd_cpx.a
    MG_CUDA_LIB = $(BIN_DIR)/mgcfd_cpx_cuda.a
endif

ifdef SIMPIC
    SIMPIC_DEF = -Ddefsimpic
	SIMPIC_LIB = $(SIMPIC_INSTALL_PATH)/libsimpic.a
endif

ifdef DEBUG
  OPTIMISE := -pg -g -O0
else
  OPTIMISE := -O3
endif
#
# Locate MPI compilers:
#
ifdef MPI_INSTALL_PATH
  ifneq ("","$(wildcard $(MPI_INSTALL_PATH)/bin/mpicxx)")
    MPICPP := $(MPI_INSTALL_PATH)/bin/mpicxx
  else
  ifneq ("","$(wildcard $(MPI_INSTALL_PATH)/intel64/bin/mpicxx)")
    MPICPP := $(MPI_INSTALL_PATH)/intel64/bin/mpicxx
  else
    MPICPP := mpicxx
  endif
  endif

  ifneq ("","$(wildcard $(MPI_INSTALL_PATH)/bin/mpicc)")
    MPICC := $(MPI_INSTALL_PATH)/bin/mpicc
  else
  ifneq ("","$(wildcard $(MPI_INSTALL_PATH)/intel64/bin/mpicc)")
    MPICC := $(MPI_INSTALL_PATH)/intel64/bin/mpicc
  else
    MPICC := mpicc
  endif
  endif
else
  MPICPP := mpicxx
  MPICC  := mpicc
endif

ifdef OP2_COMPILER
  ifeq ($(COMPILER),)
    COMPILER=$(OP2_COMPILER)
  endif
endif
ifeq ($(COMPILER),gnu)
  CPP := CC
  CFLAGS	= -fPIC -DUNIX -Wall -Wextra
  CPPFLAGS 	= $(CFLAGS)
  OMPFLAGS 	= -fopenmp
  MPIFLAGS 	= $(CPPFLAGS)
  MPICPP = CC
else
ifeq ($(COMPILER),intel)
  CPP = icpc
  CFLAGS = -DMPICH_IGNORE_CXX_SEEK -inline-forceinline -DVECTORIZE -qopt-report=5
  CFLAGS += -restrict
  # CFLAGS += -parallel ## This flag intoduces a significant slowdown into 'vec' app
  # CFLAGS += -fno-alias ## This flag causes 'vec' app to fail validation, do not enable
  CFLAGS += -fmax-errors=1
  CPPFLAGS = $(CFLAGS)
  OMPFLAGS = -qopenmp
  OMPOFFLOAD = -qopenmp
  # NVCCFLAGS += -ccbin=$(MPICPP)
  MPIFLAGS	= $(CPPFLAGS)
  ifdef ISET
    OPTIMISE += -x$(ISET)
  else
    OPTIMISE += -xHost
  endif
else
ifeq ($(COMPILER),xl)
  CPP		 = xlc++
  CFLAGS	 = -qarch=pwr8 -qtune=pwr8 -qhot
  CPPFLAGS 	 = $(CFLAGS)
  OMPFLAGS	 = -qsmp=omp -qthreaded
  OMPOFFLOAD = -qsmp=omp -qoffload -Xptxas -v -g1
  MPIFLAGS	 = $(CPPFLAGS)
else
ifeq ($(COMPILER),pgi)
  CPP       	= pgc++
  CFLAGS  	=
  CPPFLAGS 	= $(CFLAGS)
  OMPFLAGS 	= -mp
  MPIFLAGS 	= $(CPPFLAGS)
  # NVCCFLAGS	+= -ccbin=$(MPICPP)
  # ACCFLAGS      = -acc -Minfo=acc -ta=tesla:cc35 -DOPENACC
  # ACCFLAGS      = -acc -DOPENACC -Minfo=acc
  ACCFLAGS      = -v -acc -DOPENACC -Minfo=acc
else
ifeq ($(COMPILER),cray)
  CPP           = CC
  CFLAGS        = -h fp3 -h ipa5
  CPPFLAGS      = $(CFLAGS)
  OMPFLAGS      = -h omp
  MPICPP        = CC
  MPIFLAGS      = $(CPPFLAGS)
else
  $(error unrecognised value for COMPILER: $(COMPILER))
endif
endif
endif
endif
endif

ifdef CPP_WRAPPER
  CPP := $(CPP_WRAPPER)
endif
ifdef MPICPP_WRAPPER
  MPICPP := $(MPICPP_WRAPPER)
endif

all: cpx
parallel: N = $(shell nproc)
parallel:; @$(MAKE) -j$(N) -l$(N) all

## User-friendly wrappers around actual targets:
cpx: $(CPX_BIN_DIR)/cpx_runtime
cpx_cuda: $(CPX_BIN_DIR)/cpx_cuda_runtime

MPI_CPX_MAIN := $(SRC_DIR)/coupler.cpp

## CPX MPI EXECUTABLE
$(CPX_BIN_DIR)/cpx_runtime: $(MPI_CPX_MAIN) $(MG_LIB) $(FENICS_LIB) $(SIMPIC_LIB)
	mkdir -p $(CPX_BIN_DIR)
	$(MPICPP) $(CPPFLAGS) $(OPTIMISE) $^ $(MG_LIB) $(SIMPIC_LIB) $(MGCFD_LIBS) \
        -lm $(OP2_LIB) -lop2_mpi $(PARMETIS_LIB) $(DOLFINX_LIB) $(PETSC_LIB) $(BOOST_LIB)\
		$(SQLITE_LIB) $(TREETIMER_INC) $(TREETIMER_LIB) \
        $(PTSCOTCH_LIB) $(HDF5_LIB) $(FENICS_DEF) $(SIMPIC_DEF) $(MGCFD_DEF) -o $@ 

## CPX MPI CUDA EXECUTABLE
$(CPX_BIN_DIR)/cpx_cuda_runtime: $(MPI_CPX_MAIN) $(MG_CUDA_LIB) $(FENICS_LIB) $(SIMPIC_LIB)
	mkdir -p $(CPX_BIN_DIR)
	$(MPICPP) $(CPPFLAGS) $(OPTIMISE) $^ $(MG_CUDA_LIB) $(SIMPIC_LIB) $(MGCFD_LIBS) \
        $(CUDA_LIB) -lcudart $(OP2_LIB) -lop2_mpi_cuda $(PARMETIS_LIB) $(DOLFINX_LIB) $(PETSC_LIB) \
		$(SQLITE_LIB) $(TREETIMER_INC) $(TREETIMER_LIB) \
        $(PTSCOTCH_LIB) $(HDF5_LIB) $(FENICS_DEF) $(SIMPIC_DEF) -o $@ 

clean:
	rm -f $(CPX_BIN_DIR)/* $(OBJ_DIR)/*


