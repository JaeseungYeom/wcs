/******************************************************************************
 *                                                                            *
 *    Copyright 2020   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef __WCS_SIM_METHODS_SIM_METHOD_HPP__
#define __WCS_SIM_METHODS_SIM_METHOD_HPP__

#if defined(WCS_HAS_CONFIG)
#include "wcs_config.hpp"
#else
#error "no config"
#endif

#include <cmath>
#include <limits>
#include <unordered_map>
#include <memory> // unique_ptr
#include "sim_methods/sim_state_change.hpp"
#include "utils/rngen.hpp"
#include "utils/trace_ssa.hpp"
#include "utils/trace_generic.hpp"
#include "utils/samples_ssa.hpp"

namespace wcs {
/** \addtogroup wcs_sim_methods
 *  @{ */

class Sim_Method {
public:
  using v_desc_t = wcs::Network::v_desc_t;
  using sim_time_t = wcs::sim_time_t;
  using reaction_rate_t = wcs::reaction_rate_t;
  using revent_t = Sim_State_Change::revent_t;

  /** Type for the list of reactions that share any of the species with the
   *  firing reaction */
  using affected_reactions_t = Sim_State_Change::affected_reactions_t;

  enum result_t {Success, Empty, Inactive};

  Sim_Method(const std::shared_ptr<wcs::Network>& net_ptr);
  Sim_Method(Sim_Method&& other) = default;
  Sim_Method& operator=(Sim_Method&& other) = default;

  virtual ~Sim_Method();
  virtual void init(const sim_iter_t max_iter,
                    const double max_time,
                    const unsigned rng_seed) = 0;

  /// Enable tracing to record state at every event
  template <typename T>
  void set_tracing(const std::string outfile = "",
                   const unsigned frag_size = default_frag_size);

  /// Enable sampling to record state at every given time interval
  template <typename S>
  void set_sampling(const sim_time_t time_interval,
                    const std::string outfile = "",
                    const unsigned frag_size = default_frag_size);

  /// Enable sampling to record state at every given iteration interval
  template <typename S>
  void set_sampling(const sim_iter_t iter_interval,
                    const std::string outfile = "",
                    const unsigned frag_size = default_frag_size);

  /// Disable trajectory recording (tracing/sampling)
  void unset_recording();

  /// Record the initial state of simulation for tracing/sampling
  void initialize_recording(const std::shared_ptr<wcs::Network>& net_ptr);

  /// Record the state at current step
  void record(const v_desc_t rv);

  /**
   *  Record the reaction that updated the state at time t. This allows
   *  tracing/sampling using a history of events rather than as they occur.
   */
  void record(const sim_time_t t, const v_desc_t rv);

  /// Record the updates in species counts at the current step
  void record(cnt_updates_t&& u);

  /// Record the the state update at time t
  void record(const sim_time_t t, cnt_updates_t&& u);

  /// Record the updates in species concentration at the current step
  void record(conc_updates_t&& u);

  /// Record the the state update at time t
  void record(const sim_time_t t, conc_updates_t&& u);

 #if defined(WCS_HAS_ROSS)
  /**
   * Record as many states as the given number of iterations from the beginning
   * of the digest list */
  virtual void record_first_n(const sim_iter_t num) = 0;
 #endif // defined(WCS_HAS_ROSS)

 #if defined(WCS_HAS_ROSS) || (defined(_OPENMP) && defined(WCS_OMP_RUN_PARTITION))
  size_t m_lp_idx;
 #endif // defined(WCS_HAS_ROSS) || (defined(_OPENMP) && defined(WCS_OMP_RUN_PARTITION))
 #if defined(_OPENMP)
  /**
   * Set the number of omp threads to use. By default it is set to the value
   * returned by omp_get_max_threads(). If it has to be different, call this
   * function before calling `init()`.
   */
  void set_num_threads(int n) { m_num_threads = n; }
  int get_num_threads() const { return m_num_threads; }
 #endif // defined(_OPENMP)

  /// Finalize the internal trajectory recorder
  void finalize_recording();

  virtual std::pair<sim_iter_t, sim_time_t> run() = 0;

  bool fire_reaction(Sim_State_Change& digest);

 #ifdef ENABLE_SPECIES_UPDATE_TRACKING
  void undo_species_updates(const cnt_updates_t& updates) const;
 #endif // ENABLE_SPECIES_UPDATE_TRACKING
  bool undo_reaction(const Sim_Method::v_desc_t& rd_undo) const;

  sim_iter_t get_max_iter() const;
  sim_time_t get_max_time() const;

  sim_iter_t get_sim_iter() const;
  sim_time_t get_sim_time() const;

  void stop_sim();

protected:

  /** The pointer to the reaction network being monitored.
   *  Make sure the network object does not get destroyed
   *  while the trace refers to it.
   */
  std::shared_ptr<wcs::Network> m_net_ptr;

  sim_iter_t m_max_iter; ///< Upper bound on simulation iteration
  sim_time_t m_max_time; ///< Upper bound on simulation time

  sim_iter_t m_sim_iter; ///< Current simulation iteration
  sim_time_t m_sim_time; ///< Current simulation time

  bool m_recording; ///< Whether to enable tracing or sampling

  std::unique_ptr<Trajectory> m_trajectory; ///< Trajectory recorder

 #if defined(_OPENMP)
  int m_num_threads;
 #endif // defined(_OPENMP)
};


template <typename T>
void Sim_Method::set_tracing(const std::string outfile,
                             const unsigned frag_size)
{
  if (!m_trajectory) {
    m_trajectory = std::make_unique<T>(m_net_ptr);
    if (!m_trajectory) {
      WCS_THROW("Cannot start tracing.");
    }
  }
  m_recording = true;
  m_trajectory->set_outfile(outfile, frag_size);
}

template <typename S>
void Sim_Method::set_sampling(const sim_time_t time_interval,
                              const std::string outfile,
                              const unsigned frag_size)
{
  if (!m_trajectory) {
    m_trajectory = std::make_unique<S>(m_net_ptr);
    if (!m_trajectory) {
      WCS_THROW("Cannot start sampling.");
    }
  }
  m_recording = true;
  dynamic_cast<S&>(*m_trajectory).set_time_interval(time_interval);
  m_trajectory->set_outfile(outfile, frag_size);
}

template <typename S>
void Sim_Method::set_sampling(const sim_iter_t iter_interval,
                              const std::string outfile,
                              const unsigned frag_size)
{
  if (!m_trajectory) {
    m_trajectory = std::make_unique<S>(m_net_ptr);
    if (!m_trajectory) {
      WCS_THROW("Cannot start sampling.");
    }
  }
  m_recording = true;
  dynamic_cast<S&>(*m_trajectory).set_iter_interval(iter_interval);
  m_trajectory->set_outfile(outfile, frag_size);
}

/**@}*/
} // end of namespace wcs
#endif // __WCS_SIM_METHODS_SIM_METHOD_HPP__
