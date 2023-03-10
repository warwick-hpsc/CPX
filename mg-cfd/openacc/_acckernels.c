//
// auto-generated by op2.py
//

// global constants

// header
#include "op_lib_c.h"

void op_decl_const_char(int dim, char const *type,
int size, char *dat, char const *name){}
// user kernel files
#include "initialize_variables_kernel_acckernel.c"
#include "zero_5d_array_kernel_acckernel.c"
#include "zero_1d_array_kernel_acckernel.c"
#include "calculate_cell_volumes_acckernel.c"
#include "dampen_ewt_acckernel.c"
#include "copy_double_kernel_acckernel.c"
#include "calculate_dt_kernel_acckernel.c"
#include "get_min_dt_kernel_acckernel.c"
#include "compute_step_factor_kernel_acckernel.c"
#include "compute_flux_edge_kernel_acckernel.c"
#include "compute_bnd_node_flux_kernel_acckernel.c"
#include "time_step_kernel_acckernel.c"
#include "indirect_rw_kernel_acckernel.c"
#include "residual_kernel_acckernel.c"
#include "calc_rms_kernel_acckernel.c"
#include "count_bad_vals_acckernel.c"
#include "up_pre_kernel_acckernel.c"
#include "up_kernel_acckernel.c"
#include "up_post_kernel_acckernel.c"
#include "down_v2_kernel_pre_acckernel.c"
#include "down_v2_kernel_acckernel.c"
#include "down_v2_kernel_post_acckernel.c"
#include "down_kernel_acckernel.c"
#include "identify_differences_acckernel.c"
#include "count_non_zeros_acckernel.c"
