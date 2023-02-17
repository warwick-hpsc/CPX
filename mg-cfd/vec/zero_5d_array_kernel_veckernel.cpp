//
// auto-generated by op2.py
//

//user function
#ifndef MISC_H
#define MISC_H

#include <cmath>

#include "const.h"
#include "structures.h"
#include "global.h"

inline void initialize_variables_kernel(
    double* variables)
{
    for(int j = 0; j < NVAR; j++) {
        variables[j] = ff_variable[j];
    }
}

inline void zero_5d_array_kernel(
    double* array)
{
    for(int j = 0; j < NVAR; j++) {
        array[j] = 0.0;
    }
}

inline void zero_3d_array_kernel(
    double* array)
{
    for(int j = 0; j < NDIM; j++) {
        array[j] = 0.0;
    }
}

inline void zero_1d_array_kernel(
    double* array)
{
    *array = 0.0;
}

inline void calculate_cell_volumes(
    const double* coords1, 
    const double* coords2, 
    double* ewt,
    double* vol1, 
    double* vol2)
{
    double d[NDIM];
    double dist = 0.0;
    for (int i=0; i<NDIM; i++) {
        d[i] = coords2[i] - coords1[i];
        dist += d[i]*d[i];
    }
    dist = sqrt(dist);

    double area = 0.0;
    for (int i=0; i<NDIM; i++) {
        area += ewt[i]*ewt[i];
    }
    area = sqrt(area);

    double tetra_volume = (1.0/3.0)*0.5 *dist *area;
    *vol1 += tetra_volume;
    *vol2 += tetra_volume;

    // Redirect ewt to be parallel to normal:
    for (int i=0; i<NDIM; i++) {
        ewt[i] = (d[i] / dist) * area;
    }

    // |ewt| currently is face area. Divide through by distance 
    // to produce 'surface vector' with magnitude (area/dm), 
    // for use in flux accumulation:
    for (int i=0; i<NDIM; i++) {
        ewt[i] /= dist;
    }
}

inline void dampen_ewt(
    double* ewt)
{
    ewt[0] *= 1e-9;
    ewt[1] *= 1e-9;
    ewt[2] *= 1e-9;
}

#endif

// host stub function
void op_par_loop_zero_5d_array_kernel(char const *name, op_set set,
  op_arg arg0){

  int nargs = 1;
  op_arg args[1];

  args[0] = arg0;
  //create aligned pointers for dats
  ALIGNED_double       double * __restrict__ ptr0 = (double *) arg0.data;
  DECLARE_PTR_ALIGNED(ptr0,double_ALIGN);

  // initialise timers
  double cpu_t1, cpu_t2, wall_t1, wall_t2;
  op_timing_realloc(1);
  op_timers_core(&cpu_t1, &wall_t1);


  if (OP_diags>2) {
    printf(" kernel routine w/o indirection:  zero_5d_array_kernel");
  }

  int exec_size = op_mpi_halo_exchanges(set, nargs, args);

  if (exec_size >0) {

    #ifdef VECTORIZE
    #pragma novector
    for ( int n=0; n<(exec_size/SIMD_VEC)*SIMD_VEC; n+=SIMD_VEC ){
      #pragma omp simd simdlen(SIMD_VEC)
      for ( int i=0; i<SIMD_VEC; i++ ){
        zero_5d_array_kernel(
          &(ptr0)[5 * (n+i)]);
      }
    }
    //remainder
    for ( int n=(exec_size/SIMD_VEC)*SIMD_VEC; n<exec_size; n++ ){
    #else
    for ( int n=0; n<exec_size; n++ ){
    #endif
      zero_5d_array_kernel(
        &(ptr0)[5*n]);
    }
  }

  // combine reduction data
  op_mpi_set_dirtybit(nargs, args);

  // update kernel record
  op_timers_core(&cpu_t2, &wall_t2);
  OP_kernels[1].name      = name;
  OP_kernels[1].count    += 1;
  OP_kernels[1].time     += wall_t2 - wall_t1;
  OP_kernels[1].transfer += (float)set->size * arg0.size * 2.0f;
}
