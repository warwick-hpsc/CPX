//
// auto-generated by op2.py
//

#include <math.h>
#include <cmath>
#include "const.h"
#include "inlined_funcs.h"

void calculate_dt_kernel_omp4_kernel(
  double *data0,
  int dat0size,
  double *data1,
  int dat1size,
  double *data2,
  int dat2size,
  int count,
  int num_teams,
  int nthread){

  #pragma omp target teams num_teams(num_teams) thread_limit(nthread) map(to:data0[0:dat0size],data1[0:dat1size],data2[0:dat2size])
  #pragma omp distribute parallel for schedule(static,1)
  for ( int n_op=0; n_op<count; n_op++ ){
    //variable mapping
    const double* variable = &data0[5*n_op];
    const double* volume = &data1[1*n_op];
    double* dt = &data2[1*n_op];

    //inline function
    
      double density = variable[VAR_DENSITY];

      double3 momentum;
      momentum.x = variable[VAR_MOMENTUM+0];
      momentum.y = variable[VAR_MOMENTUM+1];
      momentum.z = variable[VAR_MOMENTUM+2];

      double density_energy = variable[VAR_DENSITY_ENERGY];
      double3 velocity; compute_velocity(density, momentum, velocity);
      double speed_sqd      = compute_speed_sqd(velocity);
      double pressure       = compute_pressure(density, density_energy, speed_sqd);
      double speed_of_sound = compute_speed_of_sound(density, pressure);

      *dt = double(0.5) * (cbrt(*volume) / (sqrt(speed_sqd) + speed_of_sound));
    //end inline func
  }

}
