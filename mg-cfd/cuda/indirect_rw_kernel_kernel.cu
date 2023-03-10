//
// auto-generated by op2.py
//

//user function
__device__ void indirect_rw_kernel_gpu( 
    const double *variables_a,
    const double *variables_b,
    const double *edge_weight,
    double *fluxes_a,
    double *fluxes_b) {
    double ex = edge_weight[0];
    double ey = edge_weight[1];
    double ez = edge_weight[2];

    double p_a, pe_a;
    double3 momentum_a;
    p_a          = variables_a[VAR_DENSITY];
    momentum_a.x = variables_a[VAR_MOMENTUM+0];
    momentum_a.y = variables_a[VAR_MOMENTUM+1];
    momentum_a.z = variables_a[VAR_MOMENTUM+2];
    pe_a         = variables_a[VAR_DENSITY_ENERGY];

    double p_b, pe_b;
    double3 momentum_b;
    p_b          = variables_b[VAR_DENSITY];
    momentum_b.x = variables_b[VAR_MOMENTUM+0];
    momentum_b.y = variables_b[VAR_MOMENTUM+1];
    momentum_b.z = variables_b[VAR_MOMENTUM+2];
    pe_b         = variables_b[VAR_DENSITY_ENERGY];

    double p_a_val  = p_b + ex;
    double pe_a_val = pe_b + ey;
    double mx_a_val = momentum_b.x + ez;
    double my_a_val = momentum_b.y;
    double mz_a_val = momentum_b.z;

    double p_b_val = p_a;
    double pe_b_val = pe_a;
    double mx_b_val = momentum_a.x;
    double my_b_val = momentum_a.y;
    double mz_b_val = momentum_a.z;

    fluxes_a[VAR_DENSITY]  += p_a_val;
    fluxes_a[VAR_MOMENTUM+0] += mx_a_val;
    fluxes_a[VAR_MOMENTUM+1] += my_a_val;
    fluxes_a[VAR_MOMENTUM+2] += mz_a_val;
    fluxes_a[VAR_DENSITY_ENERGY] += pe_a_val;

    fluxes_b[VAR_DENSITY]  += p_b_val;
    fluxes_b[VAR_MOMENTUM+0] += mx_b_val;
    fluxes_b[VAR_MOMENTUM+1] += my_b_val;
    fluxes_b[VAR_MOMENTUM+2] += mz_b_val;
    fluxes_b[VAR_DENSITY_ENERGY] += pe_b_val;

}

// CUDA kernel function
__global__ void op_cuda_indirect_rw_kernel(
  const double *__restrict ind_arg0,
  double *__restrict ind_arg1,
  const int *__restrict opDat0Map,
  const double *__restrict arg2,
  int start,
  int end,
  int   set_size) {
  int tid = threadIdx.x + blockIdx.x * blockDim.x;
  if (tid + start < end) {
    int n = tid + start;
    //initialise local variables
    double arg3_l[5];
    for ( int d=0; d<5; d++ ){
      arg3_l[d] = ZERO_double;
    }
    double arg4_l[5];
    for ( int d=0; d<5; d++ ){
      arg4_l[d] = ZERO_double;
    }
    int map0idx;
    int map1idx;
    map0idx = opDat0Map[n + set_size * 0];
    map1idx = opDat0Map[n + set_size * 1];

    //user-supplied kernel call
    indirect_rw_kernel_gpu(ind_arg0+map0idx*5,
                       ind_arg0+map1idx*5,
                       arg2+n*3,
                       arg3_l,
                       arg4_l);
    atomicAdd(&ind_arg1[0+map0idx*5],arg3_l[0]);
    atomicAdd(&ind_arg1[1+map0idx*5],arg3_l[1]);
    atomicAdd(&ind_arg1[2+map0idx*5],arg3_l[2]);
    atomicAdd(&ind_arg1[3+map0idx*5],arg3_l[3]);
    atomicAdd(&ind_arg1[4+map0idx*5],arg3_l[4]);
    atomicAdd(&ind_arg1[0+map1idx*5],arg4_l[0]);
    atomicAdd(&ind_arg1[1+map1idx*5],arg4_l[1]);
    atomicAdd(&ind_arg1[2+map1idx*5],arg4_l[2]);
    atomicAdd(&ind_arg1[3+map1idx*5],arg4_l[3]);
    atomicAdd(&ind_arg1[4+map1idx*5],arg4_l[4]);
  }
}


//host stub function
void op_par_loop_indirect_rw_kernel(char const *name, op_set set,
  op_arg arg0,
  op_arg arg1,
  op_arg arg2,
  op_arg arg3,
  op_arg arg4){

  int nargs = 5;
  op_arg args[5];

  args[0] = arg0;
  args[1] = arg1;
  args[2] = arg2;
  args[3] = arg3;
  args[4] = arg4;

  // initialise timers
  double cpu_t1, cpu_t2, wall_t1, wall_t2;
  op_timing_realloc(12);
  op_timers_core(&cpu_t1, &wall_t1);
  OP_kernels[12].name      = name;
  OP_kernels[12].count    += 1;


  int    ninds   = 2;
  int    inds[5] = {0,0,-1,1,1};

  if (OP_diags>2) {
    printf(" kernel routine with indirection: indirect_rw_kernel\n");
  }
  int set_size = op_mpi_halo_exchanges_cuda(set, nargs, args);
  if (set_size > 0) {

    //set CUDA execution parameters
    #ifdef OP_BLOCK_SIZE_12
      int nthread = OP_BLOCK_SIZE_12;
    #else
      int nthread = OP_block_size;
    #endif

    for ( int round=0; round<2; round++ ){
      if (round==1) {
        op_mpi_wait_all_cuda(nargs, args);
      }
      int start = round==0 ? 0 : set->core_size;
      int end = round==0 ? set->core_size : set->size + set->exec_size;
      if (end-start>0) {
        int nblocks = (end-start-1)/nthread+1;
        op_cuda_indirect_rw_kernel<<<nblocks,nthread>>>(
        (double *)arg0.data_d,
        (double *)arg3.data_d,
        arg0.map_data_d,
        (double*)arg2.data_d,
        start,end,set->size+set->exec_size);
      }
    }
  }
  op_mpi_set_dirtybit_cuda(nargs, args);
  cutilSafeCall(cudaDeviceSynchronize());
  //update kernel record
  op_timers_core(&cpu_t2, &wall_t2);
  OP_kernels[12].time     += wall_t2 - wall_t1;
}
