//
// auto-generated by op2.py
//

//user function
#include <math.h>
#include "const.h"

//user function
//#pragma acc routine
inline void up_kernel_openacc( 
    const double* variable,
    double* variable_above,
    int* up_scratch) {
    variable_above[VAR_DENSITY]        += variable[VAR_DENSITY];
    variable_above[VAR_MOMENTUM+0]     += variable[VAR_MOMENTUM+0];
    variable_above[VAR_MOMENTUM+1]     += variable[VAR_MOMENTUM+1];
    variable_above[VAR_MOMENTUM+2]     += variable[VAR_MOMENTUM+2];
    variable_above[VAR_DENSITY_ENERGY] += variable[VAR_DENSITY_ENERGY];
    *up_scratch += 1;
}

// host stub function
void op_par_loop_up_kernel(char const *name, op_set set,
  op_arg arg0,
  op_arg arg1,
  op_arg arg2){

  int nargs = 3;
  op_arg args[3];

  args[0] = arg0;
  args[1] = arg1;
  args[2] = arg2;

  // initialise timers
  double cpu_t1, cpu_t2, wall_t1, wall_t2;
  op_timing_realloc(17);
  op_timers_core(&cpu_t1, &wall_t1);
  OP_kernels[17].name      = name;
  OP_kernels[17].count    += 1;

  int  ninds   = 2;
  int  inds[3] = {-1,0,1};

  if (OP_diags>2) {
    printf(" kernel routine with indirection: up_kernel\n");
  }

  // get plan
  #ifdef OP_PART_SIZE_17
    int part_size = OP_PART_SIZE_17;
  #else
    int part_size = OP_part_size;
  #endif

  int set_size = op_mpi_halo_exchanges_cuda(set, nargs, args);


  int ncolors = 0;

  if (set_size >0) {


    //Set up typed device pointers for OpenACC
    int *map1 = arg1.map_data_d;

    double* data0 = (double*)arg0.data_d;
    double *data1 = (double *)arg1.data_d;
    int *data2 = (int *)arg2.data_d;

    op_plan *Plan = op_plan_get_stage(name,set,part_size,nargs,args,ninds,inds,OP_COLOR2);
    ncolors = Plan->ncolors;
    int *col_reord = Plan->col_reord;
    int set_size1 = set->size + set->exec_size;

    // execute plan
    for ( int col=0; col<Plan->ncolors; col++ ){
      if (col==1) {
        op_mpi_wait_all_cuda(nargs, args);
      }
      int start = Plan->col_offsets[0][col];
      int end = Plan->col_offsets[0][col+1];

      #pragma acc parallel loop independent deviceptr(col_reord,map1,data0,data1,data2)
      for ( int e=start; e<end; e++ ){
        int n = col_reord[e];
        int map1idx;
        map1idx = map1[n + set_size1 * 0];


        up_kernel_openacc(
          &data0[5 * n],
          &data1[5 * map1idx],
          &data2[1 * map1idx]);
      }

    }
    OP_kernels[17].transfer  += Plan->transfer;
    OP_kernels[17].transfer2 += Plan->transfer2;
  }

  if (set_size == 0 || set_size == set->core_size || ncolors == 1) {
    op_mpi_wait_all_cuda(nargs, args);
  }
  // combine reduction data
  op_mpi_set_dirtybit_cuda(nargs, args);

  // update kernel record
  op_timers_core(&cpu_t2, &wall_t2);
  OP_kernels[17].time     += wall_t2 - wall_t1;
}
