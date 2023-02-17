//
// auto-generated by op2.py
//

//user function
#include "utils.h"

//user function
//#pragma acc routine
inline void calc_rms_kernel_openacc( 
    const double* residual,
    double* rms) {
    for (int i=0; i<NVAR; i++) {
        *rms += residual[i]*residual[i];
    }
}

// host stub function
void op_par_loop_calc_rms_kernel(char const *name, op_set set,
  op_arg arg0,
  op_arg arg1){

  double*arg1h = (double *)arg1.data;
  int nargs = 2;
  op_arg args[2];

  args[0] = arg0;
  args[1] = arg1;

  // initialise timers
  double cpu_t1, cpu_t2, wall_t1, wall_t2;
  op_timing_realloc(14);
  op_timers_core(&cpu_t1, &wall_t1);
  OP_kernels[14].name      = name;
  OP_kernels[14].count    += 1;


  if (OP_diags>2) {
    printf(" kernel routine w/o indirection:  calc_rms_kernel");
  }

  int set_size = op_mpi_halo_exchanges_cuda(set, nargs, args);

  double arg1_l = arg1h[0];

  if (set_size >0) {


    //Set up typed device pointers for OpenACC

    double* data0 = (double*)arg0.data_d;
    #pragma acc parallel loop independent deviceptr(data0) reduction(+:arg1_l)
    for ( int n=0; n<set->size; n++ ){
      calc_rms_kernel_openacc(
        &data0[5*n],
        &arg1_l);
    }
  }

  // combine reduction data
  arg1h[0] = arg1_l;
  op_mpi_reduce_double(&arg1,arg1h);
  op_mpi_set_dirtybit_cuda(nargs, args);

  // update kernel record
  op_timers_core(&cpu_t2, &wall_t2);
  OP_kernels[14].time     += wall_t2 - wall_t1;
  OP_kernels[14].transfer += (float)set->size * arg0.size;
}