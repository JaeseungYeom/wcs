/******************************************************************************
 *                                                                            *
 *    Copyright 2020   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include <string>
#include <iostream>
#include "params/ssa_params.hpp"
#include "utils/write_graphviz.hpp"
#include "utils/timer.hpp"
#include "reaction_network/network.hpp"
#include "sim_methods/ssa_nrm.hpp"
#include "sim_methods/ssa_direct.hpp"
#include "sim_methods/ssa_sod.hpp"
#include <csignal>

#ifdef WCS_HAS_VTUNE
__itt_domain* vtune_domain_sim = __itt_domain_create("Simulate");
__itt_string_handle* vtune_handle_sim = __itt_string_handle_create("simulate");
#endif // WCS_HAS_VTUNE

wcs::Sim_Method* ssa = nullptr;
double t_start; // simulation start time

void signalHandler(int signum)
{
std::cout << "SIGTERM interruption " << signum << std::endl;
 #if defined(_OPENMP)
  #pragma omp master
 #endif // defined(_OPENMP)
  {
    if (ssa != nullptr) {
      ssa->finalize_recording();
      signal(SIGTERM, SIG_DFL);
      signal(SIGINT, SIG_DFL);
    }
    std::cout << "Wall clock time to run simulation: "
              << wcs::get_time() - t_start << " (sec)" << std::endl;

    delete ssa;
  }
  exit(signum);
}


int main(int argc, char** argv)
{
 #ifdef WCS_HAS_VTUNE
  __itt_pause();
 #endif // WCS_HAS_VTUNE
  int rc = EXIT_SUCCESS;
  wcs::SSA_Params cfg;
  cfg.getopt(argc, argv);

  std::shared_ptr<wcs::Network> rnet_ptr = std::make_shared<wcs::Network>();
  wcs::Network& rnet = *rnet_ptr;
  rnet.load(cfg.m_infile);
  rnet.init();
  const wcs::Network::graph_t& g = rnet.graph();

  if (!cfg.m_gvizfile.empty() &&
      !wcs::write_graphviz(cfg.m_gvizfile, g))
  {
    std::cerr << "Failed to write " << cfg.m_gvizfile << std::endl;
    rc = EXIT_FAILURE;
  }

  ssa = nullptr;

  try {
    if (cfg.m_method == 0) {
      ssa = new wcs::SSA_Direct(rnet_ptr);
      std::cerr << "Direct SSA method." << std::endl;
    } else if (cfg.m_method == 1) {
      std::cerr << "Next Reaction SSA method." << std::endl;
      ssa = new wcs::SSA_NRM(rnet_ptr);
    } else if (cfg.m_method == 2) {
      std::cerr << "Sorted optimized direct SSA method." << std::endl;
      ssa = new wcs::SSA_SOD(rnet_ptr);
    } else {
      std::cerr << "Unknown SSA method (" << cfg.m_method << ')' << std::endl;
      return EXIT_FAILURE;
    }
  } catch (const std::exception& e) {
    std::cerr << "Fail to setup SSA method." << std::endl;
    return EXIT_FAILURE;
  }

  if (cfg.m_tracing) {
    ssa->set_tracing<wcs::TraceSSA>(cfg.get_outfile(), cfg.m_frag_size);
    std::cerr << "Enable tracing" << std::endl;
  } else if (cfg.m_sampling) {
    if (cfg.m_iter_interval > 0u) {
      ssa->set_sampling<wcs::SamplesSSA>(cfg.m_iter_interval,
                                         cfg.get_outfile(), cfg.m_frag_size);
      std::cerr << "Enable sampling at " << cfg.m_iter_interval
                << " steps interval" << std::endl;
    } else {
      ssa->set_sampling<wcs::SamplesSSA>(cfg.m_time_interval,
                                         cfg.get_outfile(), cfg.m_frag_size);
      std::cerr << "Enable sampling at " << cfg.m_time_interval
                << " secs interval" << std::endl;
    }
  }
  ssa->init(cfg.m_max_iter, cfg.m_max_time, cfg.m_seed);

  if (cfg.m_tracing || cfg.m_sampling) {
    signal(SIGTERM, signalHandler);
    signal(SIGINT, signalHandler);
  }

 #ifdef WCS_HAS_VTUNE
  __itt_resume();
  __itt_task_begin(vtune_domain_sim, __itt_null, __itt_null, vtune_handle_sim);
 #endif // WCS_HAS_VTUNE

  t_start = wcs::get_time();
  try {
    ssa->run();
  } catch (...) {
    {
      if (cfg.m_tracing || cfg.m_sampling) {
        ssa->finalize_recording();
      }
      std::cout << "Wall clock time to run simulation: "
                << wcs::get_time() - t_start << " (sec)" << std::endl;
    }
    delete ssa;
    return EXIT_FAILURE;
  }
  std::cout << "Wall clock time to run simulation: "
            << wcs::get_time() - t_start << " (sec)" << std::endl;

 #ifdef WCS_HAS_VTUNE
  __itt_task_end(vtune_domain_sim);
  __itt_pause();
 #endif // WCS_HAS_VTUNE

  if (cfg.m_tracing || cfg.m_sampling) {
    ssa->finalize_recording();
  } else {
    std::string ofile = cfg.get_outfile();
    std::ofstream ofs(ofile);
    ofs << "Species   : " << rnet.show_species_labels("") << std::endl;
    ofs << "FinalState: " << rnet.show_species_counts() << std::endl;
  }

  delete ssa;

  return rc;
}
