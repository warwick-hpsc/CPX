// Copyright 2009, Andrew Corrigan, acorriga@gmu.edu
// This code is from the AIAA-2009-4001 paper

// Warwick extensions:
// - multigrid, per-edge computation, n-neighbours
// - residual calculation, solution validation

#include <stdio.h>
#include <iostream>
#include <fstream>
#include <cmath>
#include <string>
#include <omp.h>
#include <sys/time.h>
#include <sstream>
#include <cstdlib>
#include <map>
#include <vector>
#include <limits>
#include <chrono> 
#include "hdf5.h"

#ifdef PAPI
#include "papi_funcs.h"
#endif

// #define LOG_PROGRESS

// OP2:
#include  "op_lib_cpp.h"
#include  "op_mpi_core.h"

#include "op_hdf5.h"

// MG-CFD base:
#include "const.h"
#include "structures.h"
#include "inlined_funcs.h"
#include "config.h"
#include "utils.h"
#include "io.h"
#include "timer.h"

// Global scalars:
double smoothing_coefficient = double(0.2f);
double ff_variable[NVAR];
double ff_flux_contribution_momentum_x[NDIM];
double ff_flux_contribution_momentum_y[NDIM];
double ff_flux_contribution_momentum_z[NDIM];
double ff_flux_contribution_density_energy[NDIM];
int mesh_name;
#include "global.h"

#ifdef PAPI
int num_events;
#endif
config conf;

// MG-CFD kernels:
#include "flux.h"
#include "mg.h"
#include "time_stepping_kernels.h"
#include "compute_node_area_kernel.h"
#include "validation.h"
#include "indirect_rw.h"
#include "coupler_config.h"

int main_mgcfd(int argc, char** argv, MPI_Fint custom, int instance_number, struct unit units[], struct locators relative_positions[])
{
    #ifdef NANCHECK
        feenableexcept(FE_ALL_EXCEPT & ~FE_INEXACT);
    #endif
    set_config_defaults();
    
    if (!parse_arguments(argc, argv)) {
        return 1;
    }
    if (strcmp(conf.input_file, "") == 0) {
        op_printf("ERROR: input_file not set\n");
        return 1;
    }

    const char* input_file_name = conf.input_file;
    const char* input_directory = conf.input_file_directory;
    if (strcmp(input_directory, "")!=0) {
        input_file_name = strdup((std::string(input_directory) + "/" + input_file_name).c_str());
    }

    int problem_size = 0;
    int levels = 0;
    int base_array_index = 1;
    std::string* layers = NULL;
    std::string* mg_connectivity_filename = NULL;
    read_input_dat(input_file_name, &problem_size, &levels, &base_array_index, &layers, &mg_connectivity_filename);
    if (strcmp(input_directory, "")!=0) {
        for (int l=0; l<levels; l++) {
            layers[l] = (std::string(input_directory) + "/" + layers[l]).c_str();
            if (l < (levels-1))
                mg_connectivity_filename[l] = (std::string(input_directory) + "/" + mg_connectivity_filename[l]).c_str();
        }
    }
    
    if (base_array_index >= 1 && base_array_index <= 9) {
      // Append 'base_array_index' to args:

      char** new_argv = (char**)malloc((argc+1)*sizeof(char*));
      for (int i=0; i<argc; i++) {
        new_argv[i] = (char*)malloc((strlen(argv[i])+1)*sizeof(char));
        strcpy(new_argv[i], argv[i]);
      }
      new_argv[argc] = (char*)malloc((strlen("OP_MAPS_BASE_INDEX=0")+1)*sizeof(char));
      sprintf(new_argv[argc], "OP_MAPS_BASE_INDEX=%d", base_array_index);
      argc++;

      argv = new_argv;
    }
    
    #ifdef LOG_PROGRESS
        // op_init(argc, argv, 7); // Report positive checks in op_plan_check
        // op_init(argc, argv, 4);
        op_mpi_init_custom(argc, argv, 3, custom); // Report execution of parallel loops
        // op_init(argc, argv, 2); // Info on plan construction
        // op_init(argc, argv, 1); // Error-checking
    #else
        op_mpi_init_custom(argc, argv, 0, custom);
    #endif
    char filename[2];
    char buffer[100]; 
	char default_name[24] = "MG-CFD_output_instance_";
    sprintf(filename,"%d",instance_number);
    strcat(default_name, filename);

    FILE *fp = op_print_file_open(default_name);
    
    op_printf("MG-CFD Instance %s running!\n", filename);
    op_printf("MG-CFD Instance %s output is saved in file %s\n", filename, default_name);
    
    // timer
    double cpu_t1, cpu_t2, wall_t1, wall_t2;

    #ifdef VERIFY_OP2_TIMING
        double flux_kernel_compute_times[levels];
        for (int i=0; i<levels; i++) {
            flux_kernel_compute_times[i] = 0.0;
        }
        double flux_kernel_sync_times[levels];
        for (int i=0; i<levels; i++) {
            flux_kernel_sync_times[i] = 0.0;
        }
    #endif
    long flux_kernel_iter_counts[levels];
    for (int i=0; i<levels; i++) {
        flux_kernel_iter_counts[i] = 0;
    }
    #ifdef PAPI
        int num_events = 0;
        init_papi(&num_events);
        long_long flux_kernel_event_counts[levels*num_events];
        for (int i=0; i<(levels*num_events); i++) {
            flux_kernel_event_counts[i] = 0;
        }

        int event_set;
        int* events;
        load_papi_events(num_events, &event_set, &events);
    #endif

    // set far field conditions
    {
        const double angle_of_attack = double(PI / 180.0) * double(deg_angle_of_attack);

        ff_variable[VAR_DENSITY] = double(1.4);

        double ff_pressure = double(1.0);
        double ff_speed_of_sound = sqrt(GAMMA*ff_pressure / ff_variable[VAR_DENSITY]);
        double ff_speed = double(ff_mach)*ff_speed_of_sound;

        double3 ff_velocity;
        ff_velocity.x = ff_speed*double(cos((double)angle_of_attack));
        ff_velocity.y = ff_speed*double(sin((double)angle_of_attack));
        ff_velocity.z = 0.0;

        ff_variable[VAR_MOMENTUM+0] = ff_variable[VAR_DENSITY] * ff_velocity.x;
        ff_variable[VAR_MOMENTUM+1] = ff_variable[VAR_DENSITY] * ff_velocity.y;
        ff_variable[VAR_MOMENTUM+2] = ff_variable[VAR_DENSITY] * ff_velocity.z;

        ff_variable[VAR_DENSITY_ENERGY] = ff_variable[VAR_DENSITY]*(double(0.5)*(ff_speed*ff_speed))
                                        + (ff_pressure / double(GAMMA-1.0));

        double3 ff_momentum;
        ff_momentum.x = *(ff_variable+VAR_MOMENTUM+0);
        ff_momentum.y = *(ff_variable+VAR_MOMENTUM+1);
        ff_momentum.z = *(ff_variable+VAR_MOMENTUM+2);
        compute_flux_contribution(ff_variable[VAR_DENSITY], ff_momentum,
                                    ff_variable[VAR_DENSITY_ENERGY],
                                    ff_pressure, ff_velocity,
                                    ff_flux_contribution_momentum_x,
                                    ff_flux_contribution_momentum_y,
                                    ff_flux_contribution_momentum_z,
                                    ff_flux_contribution_density_energy);
    }

    // Set elements:
    op_set op_nodes[levels],
           op_edges[levels],
           op_bnd_nodes[levels];

    // Set mappings:
    op_map p_edge_to_nodes[levels],
           p_bnd_node_to_node[levels]
           ;

    // MG mapping:
    op_map* p_node_to_mg_node = NULL;
    op_map* p_edge_to_mg_nodes = NULL;
    if (levels > 1) {
        p_node_to_mg_node = alloc<op_map>(levels-1);
        p_edge_to_mg_nodes = alloc<op_map>(levels-1);
    }

    // Set data:
    op_dat p_edge_weights[levels],
           p_bnd_node_weights[levels],
           p_bnd_node_groups[levels],
           p_node_coords[levels];

    op_dat variables_correct[levels];

    // Temporary set data (ie, arrays that are populated by kernels)
    op_dat p_variables[levels], 
           p_old_variables[levels], 
           p_residuals[levels],
           p_residuals_prolonged[levels],
           p_residuals_prolonged_wsum[levels],
           p_volumes[levels],
           p_step_factors[levels],
           p_fluxes[levels];
    op_dat p_up_scratch[levels];

    // Setup OP2
    char* op_name = alloc<char>(100);
    {
        op_decl_const(1, "double", &smoothing_coefficient);
        op_decl_const(NVAR, "double", ff_variable);
        op_decl_const(NDIM, "double", ff_flux_contribution_momentum_x);
        op_decl_const(NDIM, "double", ff_flux_contribution_momentum_y);
        op_decl_const(NDIM, "double", ff_flux_contribution_momentum_z);
        op_decl_const(NDIM, "double", ff_flux_contribution_density_energy);
        op_decl_const(1, "int", &mesh_name);

        op_print_file("-----------------------------------------------------\n", fp);
        op_print_file("Loading from HDF5 files ...\n", fp);

        for (int i=0; i<levels; i++) {
            sprintf(buffer,"Loading level %d / %d\n", i+1, levels);
            
            op_print_file(buffer, fp);
            sprintf(op_name, "op_nodes_L%d", i);
            if (conf.legacy_mode) {
                op_nodes[i] = op_decl_set_hdf5_infer_size(layers[i].c_str(), op_name, "node_coordinates.renumbered");
            } else {
                op_nodes[i] = op_decl_set_hdf5_infer_size(layers[i].c_str(), op_name, "node_coordinates");
            }

            if (conf.legacy_mode) {
                sprintf(op_name, "op_edges_L%d", i);
                op_edges[i] = op_decl_set_hdf5_infer_size(layers[i].c_str(), op_name, "edge-->node.renumbered");
            } else {
                sprintf(op_name, "op_edges_L%d", i);
                op_edges[i] = op_decl_set_hdf5_infer_size(layers[i].c_str(), op_name, "edge-->node");
            }

            if (conf.legacy_mode) {
                sprintf(op_name, "op_bnd_nodes_L%d", i);
                op_bnd_nodes[i] = op_decl_set_hdf5_infer_size(layers[i].c_str(), op_name, "bnd_node-->node.renumbered");
            } else {
                sprintf(op_name, "op_bnd_nodes_L%d", i);
                op_bnd_nodes[i] = op_decl_set_hdf5_infer_size(layers[i].c_str(), op_name, "bnd_node-->node");
            }

            if (conf.legacy_mode) {
                p_edge_to_nodes[i]          = op_decl_map_hdf5(op_edges[i],     op_nodes[i], 2, layers[i].c_str(), "edge-->node.renumbered");
                p_bnd_node_to_node[i]       = op_decl_map_hdf5(op_bnd_nodes[i], op_nodes[i], 1, layers[i].c_str(), "bnd_node-->node.renumbered");
            } else {
                p_edge_to_nodes[i]          = op_decl_map_hdf5(op_edges[i],     op_nodes[i], 2, layers[i].c_str(), "edge-->node");
                p_bnd_node_to_node[i]       = op_decl_map_hdf5(op_bnd_nodes[i], op_nodes[i], 1, layers[i].c_str(), "bnd_node-->node");
            }
            p_bnd_node_groups[i]       = op_decl_dat_hdf5(op_bnd_nodes[i], 1, "int", layers[i].c_str(), "bnd_node-->group");

            if (i > 0) {
                sprintf(op_name, "op_node-->mg_node_L%d", i);
                if (conf.legacy_mode) {
                    p_node_to_mg_node[i-1] = op_decl_map_hdf5(op_nodes[i-1], op_nodes[i], 1, layers[i-1].c_str(), "node-->mg_node.renumbered");
                } else {
                    p_node_to_mg_node[i-1] = op_decl_map_hdf5(op_nodes[i-1], op_nodes[i], 1, layers[i-1].c_str(), "node-->mg_node");
                }

                // TODO: Generate the mapping p_edge_to_mg_nodes, where 
                //       p_edge_to_mg_nodes[i] == p_node_to_mg_node[p_edge_to_nodes[i]]
                //       It may be necessary to assume that the input mesh is so large that 
                //       it prevents generation of this mapping in a single compute node.
                p_edge_to_mg_nodes[i-1] = NULL;
            }

            sprintf(op_name, "p_volumes_L%d", i);
            if (conf.legacy_mode) {
                p_volumes[i] = op_decl_dat_hdf5(op_nodes[i], 1, "double", layers[i].c_str(), "areas");
                p_volumes[i]->name = copy_str(op_name);
            }

            if (conf.legacy_mode) {
                p_edge_weights[i] = op_decl_dat_hdf5(op_edges[i],         NDIM, "double", layers[i].c_str(), "edge_weights.recalculated");
            } else {
                p_edge_weights[i] = op_decl_dat_hdf5(op_edges[i],         NDIM, "double", layers[i].c_str(), "edge_weights");
            }
            p_bnd_node_weights[i] = op_decl_dat_hdf5(op_bnd_nodes[i], NDIM, "double", layers[i].c_str(), "bnd_node_weights");

            if (conf.legacy_mode) {
                p_node_coords[i] = op_decl_dat_hdf5(op_nodes[i], NDIM, "double", layers[i].c_str(), "node_coordinates.renumbered");
            } else {
                p_node_coords[i] = op_decl_dat_hdf5(op_nodes[i], NDIM, "double", layers[i].c_str(), "node_coordinates");
            }

            if (conf.validate_result) {
                std::string variables_solution_filepath(conf.input_file_directory);
                if (variables_solution_filepath.size() > 0) {
                    variables_solution_filepath += "/";
                }
                variables_solution_filepath += "solution.variables.L" + number_to_string(i);
                variables_solution_filepath += ".cycles=" + number_to_string(conf.num_cycles);
                variables_solution_filepath += ".h5";

                // op_printf("Checking access of file %s ...\n", variables_solution_filepath.c_str());
                if (access(variables_solution_filepath.c_str(), R_OK) != -1) {
                    std::string dataset_name("p_variables_result_L");
                    dataset_name += number_to_string(i);
                    variables_correct[i] = op_decl_dat_hdf5(op_nodes[i], NVAR, "double", variables_solution_filepath.c_str(), dataset_name.c_str());
                }
                else {
                    op_printf("Cannot find level %d solution file: %s\n", i, variables_solution_filepath.c_str());
                    variables_correct[i] = NULL;
                }
            } else {
                variables_correct[i] = NULL;
            }
        }
        op_print_file("-----------------------------------------------------\n", fp);
        op_print_file("Partitioning ...\n", fp);

        if (conf.partitioner == Partitioners::Parmetis) {
            if (conf.partitioner_method == PartitionerMethods::Geom) {
                op_partition("PARMETIS", "GEOM", op_nodes[0], OP_ID, p_node_coords[0]);
            }
            else if (conf.partitioner_method == PartitionerMethods::KWay) {
                op_partition("PARMETIS", "KWAY", op_nodes[0], p_edge_to_nodes[0], p_node_coords[0]);
            }
            else if (conf.partitioner_method == PartitionerMethods::GeomKWay) {
                op_partition("PARMETIS", "GEOMKWAY", op_nodes[0], p_edge_to_nodes[0], p_node_coords[0]);
            }
        }
        else if (conf.partitioner == Partitioners::Ptscotch) {
            if (conf.partitioner_method == PartitionerMethods::Geom) {
                op_partition("PTSCOTCH", "GEOM", op_nodes[0], OP_ID, p_node_coords[0]);
            }
            else if (conf.partitioner_method == PartitionerMethods::KWay) {
                op_partition("PTSCOTCH", "KWAY", op_nodes[0], p_edge_to_nodes[0], p_node_coords[0]);
            }
            else if (conf.partitioner_method == PartitionerMethods::GeomKWay) {
                op_partition("PTSCOTCH", "GEOMKWAY", op_nodes[0], p_edge_to_nodes[0], p_node_coords[0]);
            }
        }
        else if (conf.partitioner == Partitioners::Inertial) {
            op_partition("INERTIAL", "", op_nodes[0], OP_ID, p_node_coords[0]);
        }
        op_print_file("PARTITIONING COMPLETE\n", fp);
        op_renumber(p_edge_to_nodes[0]);

        for (int i=0; i<levels; i++) {
            sprintf(op_name, "p_variables_L%d", i);
            p_variables[i] = op_decl_dat_temp_char(op_nodes[i], NVAR, "double", sizeof(double), op_name);
            sprintf(op_name, "p_old_variables_L%d", i);
            p_old_variables[i] = op_decl_dat_temp_char(op_nodes[i], NVAR, "double", sizeof(double), op_name);
            sprintf(op_name, "p_residuals_L%d", i);
            p_residuals[i] = op_decl_dat_temp_char(op_nodes[i], NVAR, "double", sizeof(double), op_name);

            if (!conf.legacy_mode) {
                // Need to calculate cell volumes:
                sprintf(op_name, "p_volumes_L%d", i);
                p_volumes[i] = op_decl_dat_temp_char(op_nodes[i], 1, "double", sizeof(double), op_name);
            }

            sprintf(op_name, "p_step_factors_L%d", i);
            p_step_factors[i] = op_decl_dat_temp_char(op_nodes[i], 1, "double", sizeof(double), op_name);

            sprintf(op_name, "p_fluxes_L%d", i);
            p_fluxes[i] = op_decl_dat_temp_char(op_nodes[i], NVAR, "double", sizeof(double), op_name);

            if (i > 0) {
                sprintf(op_name, "p_up_scratch_L%d", i);
                p_up_scratch[i] = op_decl_dat_temp_char(op_nodes[i], 1, "int", sizeof(double), op_name);
            } else {
                p_up_scratch[i] = NULL;
            }
        }
    }

    // Initialise variables:
    for (int i=0; i<levels; i++) {
        op_par_loop(initialize_variables_kernel,"initialize_variables_kernel",op_nodes[i],
                    op_arg_dat(p_variables[i],-1,OP_ID,5,"double",OP_WRITE));
        op_par_loop(zero_5d_array_kernel,"zero_5d_array_kernel",op_nodes[i],
                    op_arg_dat(p_fluxes[i],-1,OP_ID,5,"double",OP_WRITE));

        if (!conf.legacy_mode) {
            op_par_loop(zero_1d_array_kernel,"zero_1d_array_kernel",op_nodes[i],
                        op_arg_dat(p_volumes[i],-1,OP_ID,1,"double",OP_WRITE));
            op_par_loop(calculate_cell_volumes,"calculate_cell_volumes",op_edges[i],
                        op_arg_dat(p_node_coords[i],0,p_edge_to_nodes[i],3,"double",OP_READ),
                        op_arg_dat(p_node_coords[i],1,p_edge_to_nodes[i],3,"double",OP_READ),
                        op_arg_dat(p_edge_weights[i],-1,OP_ID,3,"double",OP_INC),
                        op_arg_dat(p_volumes[i],0,p_edge_to_nodes[i],1,"double",OP_INC),
                        op_arg_dat(p_volumes[i],1,p_edge_to_nodes[i],1,"double",OP_INC));
        }
    }

    // Fudge the weights to delay occurrence of negative densities in HDF5 meshes:
    for (int l=0; l<levels; l++) {
        op_par_loop(dampen_ewt,"dampen_ewt",op_edges[l],
                    op_arg_dat(p_edge_weights[l],-1,OP_ID,3,"double",OP_INC));
        op_par_loop(dampen_ewt,"dampen_ewt",op_bnd_nodes[l],
                    op_arg_dat(p_bnd_node_weights[l],-1,OP_ID,3,"double",OP_INC));
    }

    op_print_file("-----------------------------------------------------\n", fp);
    op_print_file("Compute beginning\n", fp);
    
    op_timers(&cpu_t1, &wall_t1);

    int level = 0;
    int mg_dir = MG_UP;
    int i = 0;
    double rms = 0.0;
    int bad_val_count = 0;
    double min_dt = std::numeric_limits<double>::max();


    MPI_Comm mgcfd_comm = MPI_Comm_f2c(custom);
    int internal_rank;
    int internal_size;
    MPI_Comm_rank(mgcfd_comm, &internal_rank); 
    MPI_Comm_size(mgcfd_comm, &internal_size);
    int *ranks = new int[internal_size];
  
    MPI_Gather(&internal_rank, 1, MPI_INT, ranks, 1, MPI_INT, 0, mgcfd_comm);

    int worldrank;
    MPI_Comm_rank(MPI_COMM_WORLD, &worldrank); 
 
    int mgcfd_unit_num = relative_positions[worldrank].placelocator;
    int unit_count = 0;
    int mgcfd_count = 1;//since units start from 1
    bool found = false;
    while(!found){
        if(units[unit_count].type == 'M' && mgcfd_unit_num == mgcfd_count){
            found=true;
        }else {
            if(units[unit_count].type != 'C'){
                mgcfd_count++;
            }
            unit_count++;
        }
    }
     
    int total_coupler_unit_count = units[unit_count].coupler_ranks.size();
    int coupler_rank = units[unit_count].coupler_ranks[0][0]; //This assumes only 1 coupler unit per 2 MG-CFD sessions 
    int prev_cycle = -1;

    double nodes_size = 0;
    double boundary_nodes_size = 0;
    bool hide_search = true;
    char *data_l0;
    int null_check;

    null_check = op_get_size(op_nodes[0]);
    nodes_size = null_check;

    boundary_nodes_size = round(nodes_size * 0.0042);//set the boundary size

    op_printf("Size of interface 1: %f \n", boundary_nodes_size);
    
    int ranks_per_coupler;
    if (internal_rank == MPI_ROOT) {
        for(int z = 0; z < total_coupler_unit_count; z++){
            ranks_per_coupler = units[unit_count].coupler_ranks[z].size();
            for(int z2 = 0; z2 < ranks_per_coupler; z2++){
                MPI_Send(&boundary_nodes_size, 1, MPI_DOUBLE, units[unit_count].coupler_ranks[z][z2], 0, MPI_COMM_WORLD);//this sends the node sizes to each of the coupler ranks of each of the coupler units
            }
        }
    }

    double *p_variables_data = (double*) malloc(nodes_size * NVAR * sizeof(double));
    double *p_variables_recv = (double*) malloc(nodes_size * NVAR * sizeof(double));

    std::chrono::duration<double> total_seconds;

    while(i < conf.num_cycles)
    {
        #ifdef LOG_PROGRESS
            sprintf(buffer,"Performing MG cycle %d / %d", i+1, conf.num_cycles);
            op_print_file(buffer, fp);
        #else
            if (level==0){
                sprintf(buffer,"Performing MG cycle %d / %d", i+1, conf.num_cycles);
                op_print_file(buffer, fp);
            }
        #endif

        if((i != prev_cycle && (i % conversion_factor) == 0) || (hide_search == true && i != prev_cycle && ((i % upd_freq) == conversion_factor - 1))){
            prev_cycle=i;

            op_dat temp_dat_l0 = (op_dat) malloc(sizeof(op_dat_core));
            op_set set_l0 = (op_set) malloc(sizeof(op_set_core));

            data_l0 = (char *) malloc(op_bnd_nodes[0]->size * p_variables[0]->size);
            memcpy(data_l0, p_variables[0]->data, op_bnd_nodes[0]->size * p_variables[0]->size);

            set_l0->index = p_variables[0]->set->index;
            set_l0->size = op_bnd_nodes[0]->size;
            set_l0->name = p_variables[0]->set->name;

            temp_dat_l0->index = p_variables[0]->index;
            temp_dat_l0->set = set_l0;
            temp_dat_l0->dim = p_variables[0]->dim;
            temp_dat_l0->data = data_l0;
            temp_dat_l0->data_d = NULL;
            temp_dat_l0->name = p_variables[0]->name;
            temp_dat_l0->type = p_variables[0]->type;
            temp_dat_l0->size = p_variables[0]->size;

            op_fetch_data(temp_dat_l0, p_variables_data);
            
            if(internal_rank == MPI_ROOT){
                if(hide_search == true){
                    op_printf("Cycle %d comms starting\n", i);
                } else if (hide_search == false && ((i % upd_freq) != conversion_factor - 1)){
                    op_printf("Cycle %d comms starting\n", i);
                }

                for(int z = 0; z < total_coupler_unit_count; z++){
                    coupler_rank = units[unit_count].coupler_ranks[z][0];
					int coupler_position = relative_positions[coupler_rank].placelocator;
					found = false;
					int unit_count_2 = 0;
					int coupler_unit_count = 1;
					while(!found){//this is used to find out the unit index of the coupler unit we want
						if(units[unit_count_2].type == 'C' && coupler_unit_count == coupler_position){
						found=true;
						}else{
							if(units[unit_count_2].type == 'C'){
								coupler_unit_count++;
							}
							unit_count_2++;
						}
					}
					int coupler_vars = 0;
					if(units[unit_count_2].coupling_type == 'S'){
						coupler_vars = 5;
					}else if(units[unit_count_2].coupling_type == 'C'){
						coupler_vars = 1;
					}
                    if(hide_search == true){
                        if((i % upd_freq) == 0){
                            op_printf("Cycle %d - search taking place\n", i);
                            MPI_Send(p_variables_data, boundary_nodes_size * coupler_vars, MPI_DOUBLE, coupler_rank, 0, MPI_COMM_WORLD);
                        }else if((i % upd_freq) == conversion_factor - 1){
                            auto start = std::chrono::steady_clock::now();
                            MPI_Recv(p_variables_recv, boundary_nodes_size * coupler_vars, MPI_DOUBLE, coupler_rank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                            auto end = std::chrono::steady_clock::now();
                            std::chrono::duration<double> elapsed_seconds = end-start;
                            total_seconds += elapsed_seconds;
                        }else{
                            MPI_Send(p_variables_data, boundary_nodes_size * coupler_vars, MPI_DOUBLE, coupler_rank, 0, MPI_COMM_WORLD);
                            auto start = std::chrono::steady_clock::now();
                            MPI_Recv(p_variables_recv, boundary_nodes_size * coupler_vars, MPI_DOUBLE, coupler_rank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                            auto end = std::chrono::steady_clock::now();
                            std::chrono::duration<double> elapsed_seconds = end-start;
                            total_seconds += elapsed_seconds;
                        }
                    }else{
                        MPI_Send(p_variables_data, boundary_nodes_size * coupler_vars, MPI_DOUBLE, coupler_rank, 0, MPI_COMM_WORLD);
                        auto start = std::chrono::steady_clock::now();
                        MPI_Recv(p_variables_recv, boundary_nodes_size * coupler_vars, MPI_DOUBLE, coupler_rank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                        auto end = std::chrono::steady_clock::now();
                        std::chrono::duration<double> elapsed_seconds = end-start;
                        total_seconds += elapsed_seconds;
                    }
                }
            }

            op_printf("Cycle %d comms ending\n", i);
            free(temp_dat_l0->data);
            free(temp_dat_l0->set);
            free(temp_dat_l0);
        }
        


        op_par_loop(copy_double_kernel,"copy_double_kernel",op_nodes[level],
                    op_arg_dat(p_variables[level],-1,OP_ID,5,"double",OP_READ),
                    op_arg_dat(p_old_variables[level],-1,OP_ID,5,"double",OP_WRITE));

        // for the first iteration we compute the time step
        op_par_loop(calculate_dt_kernel,"calculate_dt_kernel",op_nodes[level],
                    op_arg_dat(p_variables[level],-1,OP_ID,5,"double",OP_READ),
                    op_arg_dat(p_volumes[level],-1,OP_ID,1,"double",OP_READ),
                    op_arg_dat(p_step_factors[level],-1,OP_ID,1,"double",OP_WRITE));
        min_dt = std::numeric_limits<double>::max();
        op_par_loop(get_min_dt_kernel,"get_min_dt_kernel",op_nodes[level],
                    op_arg_dat(p_step_factors[level],-1,OP_ID,1,"double",OP_READ),
                    op_arg_gbl(&min_dt,1,"double",OP_MIN));
        if (min_dt < 0.0f) {
          sprintf(buffer,"Fatal error during 'step factor' calculation, min_dt = %.5e\n", min_dt);
          op_print_file(buffer, fp);
          op_exit();
          return 1;
        }
        op_par_loop(compute_step_factor_kernel,"compute_step_factor_kernel",op_nodes[level],
                    op_arg_dat(p_variables[level],-1,OP_ID,5,"double",OP_READ),
                    op_arg_dat(p_volumes[level],-1,OP_ID,1,"double",OP_READ),
                    op_arg_gbl(&min_dt,1,"double",OP_READ),
                    op_arg_dat(p_step_factors[level],-1,OP_ID,1,"double",OP_WRITE));

        int rkCycle;
        for (rkCycle=0; rkCycle<RK; rkCycle++)
        {
            #ifdef LOG_PROGRESS
                sprintf(buffer," RK cycle %d / %d\n", rkCycle+1, RK);
                op_print_file(buffer, fp);
            #endif

            op_par_loop(compute_flux_edge_kernel,"compute_flux_edge_kernel",op_edges[level],
                        op_arg_dat(p_variables[level],0,p_edge_to_nodes[level],5,"double",OP_READ),
                        op_arg_dat(p_variables[level],1,p_edge_to_nodes[level],5,"double",OP_READ),
                        op_arg_dat(p_edge_weights[level],-1,OP_ID,3,"double",OP_READ),
                        op_arg_dat(p_fluxes[level],0,p_edge_to_nodes[level],5,"double",OP_INC),
                        op_arg_dat(p_fluxes[level],1,p_edge_to_nodes[level],5,"double",OP_INC));

            op_par_loop(compute_bnd_node_flux_kernel,"compute_bnd_node_flux_kernel",op_bnd_nodes[level],
                        op_arg_dat(p_bnd_node_groups[level],-1,OP_ID,1,"int",OP_READ),
                        op_arg_dat(p_bnd_node_weights[level],-1,OP_ID,3,"double",OP_READ),
                        op_arg_dat(p_variables[level],0,p_bnd_node_to_node[level],5,"double",OP_READ),
                        op_arg_dat(p_fluxes[level],0,p_bnd_node_to_node[level],5,"double",OP_INC));

            op_par_loop(time_step_kernel,"time_step_kernel",op_nodes[level],
                        op_arg_gbl(&rkCycle,1,"int",OP_READ),
                        op_arg_dat(p_step_factors[level],-1,OP_ID,1,"double",OP_READ),
                        op_arg_dat(p_fluxes[level],-1,OP_ID,5,"double",OP_INC),
                        op_arg_dat(p_old_variables[level],-1,OP_ID,5,"double",OP_READ),
                        op_arg_dat(p_variables[level],-1,OP_ID,5,"double",OP_WRITE));

            op_par_loop(indirect_rw_kernel,"indirect_rw_kernel",op_edges[level],
                        op_arg_dat(p_variables[level],0,p_edge_to_nodes[level],5,"double",OP_READ),
                        op_arg_dat(p_variables[level],1,p_edge_to_nodes[level],5,"double",OP_READ),
                        op_arg_dat(p_edge_weights[level],-1,OP_ID,3,"double",OP_READ),
                        op_arg_dat(p_fluxes[level],0,p_edge_to_nodes[level],5,"double",OP_INC),
                        op_arg_dat(p_fluxes[level],1,p_edge_to_nodes[level],5,"double",OP_INC));
            op_par_loop(zero_5d_array_kernel,"zero_5d_array_kernel",op_nodes[level],
                        op_arg_dat(p_fluxes[level],-1,OP_ID,5,"double",OP_WRITE));
        }

        op_par_loop(residual_kernel,"residual_kernel",op_nodes[level],
                    op_arg_dat(p_old_variables[level],-1,OP_ID,5,"double",OP_READ),
                    op_arg_dat(p_variables[level],-1,OP_ID,5,"double",OP_READ),
                    op_arg_dat(p_residuals[level],-1,OP_ID,5,"double",OP_WRITE));
        if (level == 0) {
            rms = 0.0;
            op_par_loop(calc_rms_kernel,"calc_rms_kernel",op_nodes[level],
                        op_arg_dat(p_residuals[level],-1,OP_ID,5,"double",OP_READ),
                        op_arg_gbl(&rms,1,"double",OP_INC));
            rms = sqrt(rms / double(op_get_size(op_nodes[level])));
            // op_printf(" (RMS = %.3e)", rms);
            // Until I get the HDF5 meshes working correctly, no point displaying incorrect RMS.

            #ifdef OPENACC
              // count_bad_vals() invokes isnan(), unsupported with OpenACC.
            #else
                op_par_loop(count_bad_vals,"count_bad_vals",op_nodes[level],
                            op_arg_dat(p_variables[level],-1,OP_ID,5,"double",OP_READ),
                            op_arg_gbl(&bad_val_count,1,"int",OP_INC));
            #endif
            if (bad_val_count > 0) {
                op_print_file("Bad variable values detected, aborting\n", fp);
                op_exit();
                return 1;
            }
            op_print_file("\n", fp);
        }

        if (levels <= 1) {
            i++;
        }
        else {
            if(mg_dir == MG_UP)
            {
                level++;

                op_par_loop(up_pre_kernel,"up_pre_kernel",op_nodes[level-1],
                            op_arg_dat(p_variables[level],0,p_node_to_mg_node[level-1],5,"double",OP_WRITE),
                            op_arg_dat(p_up_scratch[level],0,p_node_to_mg_node[level-1],1,"int",OP_WRITE));

                op_par_loop(up_kernel,"up_kernel",op_nodes[level-1],
                            op_arg_dat(p_variables[level-1],-1,OP_ID,5,"double",OP_READ),
                            op_arg_dat(p_variables[level],0,p_node_to_mg_node[level-1],5,"double",OP_INC),
                            op_arg_dat(p_up_scratch[level],0,p_node_to_mg_node[level-1],1,"int",OP_INC));

                op_par_loop(up_post_kernel,"up_post_kernel",op_nodes[level],
                            op_arg_dat(p_variables[level],-1,OP_ID,5,"double",OP_INC),
                            op_arg_dat(p_up_scratch[level],-1,OP_ID,1,"int",OP_READ));

                if(level == levels-1)
                {
                    mg_dir = MG_DOWN;
                }
            }
            else
            {
                level--;

                if (p_edge_to_mg_nodes[level] != NULL) {
                    // NOTE: Because I have not yet generated the mapping 'p_edge_to_mg_nodes', I have not 
                    // tested these 'down_v2_kernel' loops at all. I have simply ported it from MG-CFD-app-plain
                    op_par_loop(down_v2_kernel_pre,"down_v2_kernel_pre",op_nodes[level],
                                op_arg_dat(p_residuals_prolonged[level],-1,OP_ID,5,"double",OP_WRITE),
                                op_arg_dat(p_residuals_prolonged_wsum[level],-1,OP_ID,1,"double",OP_WRITE));
                    op_par_loop(down_v2_kernel,"down_v2_kernel",op_edges[level],
                                op_arg_dat(p_node_coords[level],0,p_edge_to_nodes[level],3,"double",OP_READ),
                                op_arg_dat(p_node_coords[level],1,p_edge_to_nodes[level],3,"double",OP_READ),
                                op_arg_dat(p_node_coords[level+1],0,p_edge_to_mg_nodes[level],3,"double",OP_READ),
                                op_arg_dat(p_node_coords[level+1],1,p_edge_to_mg_nodes[level],3,"double",OP_READ),
                                op_arg_dat(p_residuals[level+1],0,p_edge_to_mg_nodes[level],5,"double",OP_READ),
                                op_arg_dat(p_residuals[level+1],1,p_edge_to_mg_nodes[level],5,"double",OP_READ),
                                op_arg_dat(p_residuals_prolonged[level],0,p_edge_to_nodes[level],5,"double",OP_INC),
                                op_arg_dat(p_residuals_prolonged[level],1,p_edge_to_nodes[level],5,"double",OP_INC),
                                op_arg_dat(p_residuals_prolonged_wsum[level],0,p_edge_to_nodes[level],1,"double",OP_INC),
                                op_arg_dat(p_residuals_prolonged_wsum[level],1,p_edge_to_nodes[level],1,"double",OP_INC));
                    op_par_loop(down_v2_kernel_post,"down_v2_kernel_post",op_nodes[level],
                                op_arg_dat(p_residuals_prolonged[level],-1,OP_ID,5,"double",OP_READ),
                                op_arg_dat(p_residuals_prolonged_wsum[level],-1,OP_ID,1,"double",OP_READ),
                                op_arg_dat(p_residuals[level],-1,OP_ID,5,"double",OP_READ),
                                op_arg_dat(p_variables[level],-1,OP_ID,5,"double",OP_INC));
                } else {
                    op_par_loop(down_kernel,"down_kernel",op_nodes[level],
                                op_arg_dat(p_variables[level],-1,OP_ID,5,"double",OP_INC),
                                op_arg_dat(p_residuals[level],-1,OP_ID,5,"double",OP_READ),
                                op_arg_dat(p_node_coords[level],-1,OP_ID,3,"double",OP_READ),
                                op_arg_dat(p_residuals[level+1],0,p_node_to_mg_node[level],5,"double",OP_READ),
                                op_arg_dat(p_node_coords[level+1],0,p_node_to_mg_node[level],3,"double",OP_READ));
                }

                if(level == 0)
                {
                    mg_dir = MG_UP;
                    i++;
                }
            }
        }
    }

    op_print_file("\n", fp);
    op_print_file("Compute complete\n", fp);

    op_timers(&cpu_t2, &wall_t2);

    sprintf(buffer,"Max total runtime = %f\n", wall_t2 - wall_t1);
    op_print_file(buffer, fp);

    sprintf(buffer,"Time spent coupling = %f\n", total_seconds.count());
    op_print_file(buffer, fp);

    op_printf("MG-CFD Instance %s has finished!\n", filename);

    // Write summary performance data to stdout:
    op_printf("MG-CFD Instance %s performance summary:\n", filename);
    op_timing_output();

    // Write full performance data to file:
    std::string csv_out_filepath(conf.output_file_prefix);
    csv_out_filepath += "op2_performance_data_instance_" + std::string(filename) + ".csv";
    sprintf(buffer,"Writing MG-CFD Instance %s OP2 timings to file: %s\n", filename, csv_out_filepath.c_str());
    op_print_file(buffer, fp);

    op_timings_to_csv(csv_out_filepath.c_str());

    if (conf.validate_result) {
        op_print_file("-----------------------------------------------------\n", fp);

        bool value_check_failed = false;
        op_print_file("Looking for NaN and infinity values ...", fp);
        for (int l=0; l<levels; l++) {
            int bad_val_count = 0;
            op_par_loop(count_bad_vals,"count_bad_vals",op_nodes[l],
                        op_arg_dat(p_variables[l],-1,OP_ID,5,"double",OP_READ),
                        op_arg_gbl(&bad_val_count,1,"int",OP_INC));
            if (bad_val_count > 0) {
                value_check_failed = true;
                op_print_file("\n", fp);
                sprintf(buffer,"Value check of MG level %d failed: %d bad values detected\n", l, bad_val_count);
                break;
            }
        }
        if (!value_check_failed) {
            op_print_file(" None found\n", fp);
            bool validation_failed = false;
            op_print_file("Validating result against solution ...", fp);
            for (int l=0; l<levels; l++) {
                if (variables_correct[l] == NULL) {
                    op_print_file("\n", fp);
                    sprintf(buffer,"- Do not have solution for level %d, cannot validate\n", l);
                    op_print_file(buffer, fp);
                    validation_failed = true;
                    continue;
                }

                sprintf(op_name, "p_var_diff_L%d", l);
                op_dat variables_difference = op_decl_dat_temp_char(op_nodes[l], NVAR, "double", sizeof(double), op_name);

                op_par_loop(identify_differences,"identify_differences",op_nodes[l],
                            op_arg_dat(p_variables[l],-1,OP_ID,5,"double",OP_READ),
                            op_arg_dat(variables_correct[l],-1,OP_ID,5,"double",OP_READ),
                            op_arg_dat(variables_difference,-1,OP_ID,5,"double",OP_WRITE));

                int count = 0;
                op_par_loop(count_non_zeros,"count_non_zeros",op_nodes[l],
                            op_arg_dat(variables_difference,-1,OP_ID,5,"double",OP_READ),
                            op_arg_gbl(&count,1,"int",OP_INC));
                if (count > 0) {
                    validation_failed = true;
                    op_print_file("\n", fp);
                    sprintf(buffer,"Validation of MG level %d failed: %d incorrect values in 'variables' array\n", l, count);
                    op_print_file(buffer, fp);
                    break;
                } else {
                    // op_printf("Validation of MG level %d successful\n", l);
                }
            }

            if (validation_failed) {
                op_print_file("Validation failed\n", fp);
            } else {
                op_print_file(" Result correct\n", fp);
                op_print_file("Validation passed\n", fp);
            }
        }
    }

    if (conf.output_anything) {
        op_print_file("-----------------------------------------------------\n", fp);
        op_print_file("Writing out data...\n", fp);
        char* h5_out_name = alloc<char>(100);
        std::string prefix(conf.output_file_prefix);
        for (int l=0; l<levels; l++)
        {
            std::string suffix = std::string(".L") + number_to_string(l) 
                               + "." + "cycles=" + number_to_string(conf.num_cycles) + ".instance" + std::string(filename);

            int number_of_edges = op_get_size(op_edges[l]);
            int nel     = op_get_size(op_nodes[l]);

            // Dump volumes:
            if (conf.output_volumes) {
                const char* old_name = p_volumes[l]->name;
                sprintf(op_name, "p_volumes_result_L%d", l);
                p_volumes[l]->name = strdup(op_name);
                sprintf(h5_out_name, "%svolumes%s.h5", prefix.c_str(), suffix.c_str());
                op_fetch_data_hdf5_file(p_volumes[l], h5_out_name);
                p_volumes[l]->name = old_name;
            }

            // Dump step factors:
            if (conf.output_step_factors) {
                const char* old_name = p_step_factors[l]->name;
                sprintf(op_name, "p_step_factors_result_L%d", l);
                p_step_factors[l]->name = strdup(op_name);
                sprintf(h5_out_name, "%sstep_factors%s.h5", prefix.c_str(), suffix.c_str());
                op_fetch_data_hdf5_file(p_step_factors[l], h5_out_name);
                p_step_factors[l]->name = old_name;
            }

            // Dump fluxes:
            if (conf.output_fluxes) {
                const char* old_name = p_fluxes[l]->name;
                sprintf(op_name, "p_fluxes_result_L%d", l);
                p_fluxes[l]->name = strdup(op_name);
                sprintf(h5_out_name, "%sfluxes%s.h5", prefix.c_str(), suffix.c_str());
                op_fetch_data_hdf5_file(p_fluxes[l], h5_out_name);
                p_fluxes[l]->name = old_name;
            }
            
            // Dump variables:
            if (conf.output_variables) {
                const char* old_name = p_variables[l]->name;
                sprintf(op_name, "p_variables_result_L%d", l);
                p_variables[l]->name = strdup(op_name);
                sprintf(h5_out_name, "%svariables%s.h5", prefix.c_str(), suffix.c_str());
                op_fetch_data_hdf5_file(p_variables[l], h5_out_name);
                p_variables[l]->name = old_name;

                op_printf("Level: %d\n", l);
                for (int z = 0; z < 10; z++) {
                        if (l == 0) {
                            op_printf("OP2 fetched p_variables data[%d]: %f\n", z, p_variables_data);
                            op_printf("Received p_variables data[%d]: %f\n", z, p_variables_recv);
                        }
                        /*else if (l == 1) {
                            op_printf("OP2 fetched p_variables data[%d]: %f\n", z, p_variables_data_l1[z]);
                            op_printf("Received p_variables data[%d]: %f\n", z, p_variables_recv_l1[z]);
                        }
                        else if (l == 2) {
                            op_printf("OP2 fetched p_variables data[%d]: %f\n", z, p_variables_data_l2[z]);
                            op_printf("Received p_variables data[%d]: %f\n", z, p_variables_recv_l2[z]);
                        }
                        else if (l == 3) {
                            op_printf("OP2 fetched p_variables data[%d]: %f\n", z, p_variables_data_l3[z]);
                            op_printf("Received p_variables data[%d]: %f\n", z, p_variables_recv_l3[z]);
                        }*/
                }

            }
        }
    }

    int my_rank=0;
    #ifdef MPI_ON
    op_rank(&my_rank);
    #endif
    #ifdef PAPI
        dump_papi_counters_to_file(
            my_rank, 
            levels, 
            num_events, 
            events, 
            flux_kernel_event_counts, 
            conf.output_file_prefix);
    #endif

    #ifdef DUMP_EXT_PERF_DATA
        dump_perf_data_to_file(
            my_rank, 
            levels, 
            #ifdef VERIFY_OP2_TIMING
                flux_kernel_compute_times, 
                flux_kernel_sync_times,
            #endif
            flux_kernel_iter_counts, 
            conf.output_file_prefix);
    #endif
    
    op_print_file("-----------------------------------------------------\n", fp);
    op_print_file("Winding down OP2\n", fp);
    
    op_printf("Winding down OP2 for MG-CFD Instance %s\n", filename);

    op_print_file_close(fp);
    
    //int exit_command = 1;
    //MPI_Send(&exit_command, 1, MPI_INT, coupler_rank, 0, MPI_COMM_WORLD);
    op_exit();

    return 0;
}







