/******************************************************************************
 *                                                                            *
 *    Copyright 2020   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include <algorithm> // upper_bound
#include <cmath> // log
#include "sim_methods/ssa_direct.hpp"
#include "utils/exception.hpp"
#include "utils/seed.hpp"

#if defined(WCS_HAS_CEREAL)
#include "utils/state_io_cereal.hpp"
#endif // WCS_HAS_CEREAL

namespace wcs {
/** \addtogroup wcs_reaction_network
 *  @{ */

SSA_Direct::SSA_Direct(const std::shared_ptr<wcs::Network>& net_ptr)
: Sim_Method(net_ptr) {}

SSA_Direct::~SSA_Direct() {}

/**
 * Defines the priority queue ordering by the event propensity
 * (in ascending order).
 */
bool SSA_Direct::less(const priority_t& v1, const priority_t& v2) {
  return (v1.first < v2.first);
}

/// Allow access to the internal random number generator for events
SSA_Direct::rng_t& SSA_Direct::rgen_e() {
  return m_rgen_evt;
}

/// Allow access to the internal random number generator for event times
SSA_Direct::rng_t& SSA_Direct::rgen_t() {
  return m_rgen_tm;
}

/**
 * Initialize the reaction propensity list by filling it with the propesity of
 * every reaction and sorting.
 */
void SSA_Direct::build_propensity_list()
{
  m_propensity.clear();
  m_pindices.clear();
  const size_t num_reactions = m_net_ptr->get_num_reactions()+1;
  m_propensity.reserve(num_reactions);
  m_pindices.reserve(num_reactions);
  constexpr auto zero_rate = static_cast<reaction_rate_t>(0.0);

  for (const auto& vd : m_net_ptr->reaction_list())
  {
    const auto rate = m_net_ptr->get_reaction_rate(vd);
    m_propensity.emplace_back(priority_t(rate, vd));
  }

  std::stable_sort(m_propensity.begin(), m_propensity.end(), SSA_Direct::less);

  reaction_rate_t sum = zero_rate;
  size_t i = 0ul;
  for (auto& p : m_propensity)
  { // convert individual rate into the culumative rate up to the point in the
    // propensity list
    auto& rate = p.first;
    const auto& vd = p.second;
    sum += rate;
    rate = sum;

    m_pindices.insert(std::make_pair(vd, i++));
  }
}

/// Randomly determine which reaction to fire.
SSA_Direct::priority_t& SSA_Direct::choose_reaction()
{
  const auto rn
    = static_cast<reaction_rate_t>(m_rgen_evt() * m_propensity.back().first);
  auto it = std::upper_bound(m_propensity.begin(),
                             m_propensity.end(),
                             rn,
                             [](const double lhs, const priority_t& rhs)
                               -> bool { return lhs < rhs.first; });
  if (it == m_propensity.end()) {
    WCS_THROW("Failed to choose a reaction to fire");
  }
  return *it;
}

/// Randomly determine the time period until the next reaction
sim_time_t SSA_Direct::get_reaction_time()
{
  // total propensity
  const reaction_rate_t r = m_propensity.empty()?
                              static_cast<reaction_rate_t>(0) :
                              (m_propensity.back().first);
  return ((r <= static_cast<reaction_rate_t>(0))?
            wcs::Network::get_etime_ulimit() :
            -static_cast<reaction_rate_t>(log(m_rgen_tm())/r));
}

/**
 * Recompute the reaction rates of those affected which are linked with
 * updating species. Also, the update cumulative propensity list.
 */
void SSA_Direct::update_reactions(priority_t& fired,
  const Sim_Method::affected_reactions_t& affected_reactions,
  bool check_reaction)
{
  using r_prop_t = wcs::Reaction<v_desc_t>;
  constexpr auto zero_rate = static_cast<reaction_rate_t>(0.0);

  const auto vd_fired = fired.second;
  // Initialize the lower bound of the indices of the propensties of reactions
  // updated. This helps avoid updating the cumulative propensity from the
  // begining of the propensity vector.
  size_t pidx_min = m_pindices.at(vd_fired);
  if (check_reaction && !m_net_ptr->check_reaction(vd_fired)) {
    fired.first = zero_rate;
  } else {
    // update the propensity of the fired reaction
    fired.first = m_net_ptr->set_reaction_rate(vd_fired);
  }

  // update the propensity of the rest of affected reactions
  for (const auto& vd : affected_reactions) {
    const size_t pidx = m_pindices.at(vd);
    pidx_min = ((pidx < pidx_min)? pidx : pidx_min);
    // For reverse computation, this could have been restored from memory
    // instead of computation.

    if (check_reaction && !m_net_ptr->check_reaction(vd)) {
      (m_propensity.at(pidx)).first = zero_rate;
    } else {
      (m_propensity.at(pidx)).first = m_net_ptr->set_reaction_rate(vd);
    }
  }

  reaction_rate_t sum = (pidx_min > 0ul)?
                        (m_propensity.at(pidx_min-1)).first : zero_rate;

  const wcs::Network::graph_t& g = m_net_ptr->graph();

  // update the cumulative propensity
  for (size_t i = pidx_min; i < m_propensity.size(); ++ i) {
    auto& prop = m_propensity.at(i);
    const auto vd = prop.second;
    const auto& rv = g[vd]; // reaction vertex
    const auto& rp = rv.property<r_prop_t>(); // detailed vertex property data
    sum += rp.get_rate(); // cumulative reaction propensity
    prop.first = sum;
  }
}


void SSA_Direct::init(const sim_iter_t max_iter,
                      const double max_time,
                      const unsigned rng_seed)
{
  if (!m_net_ptr) {
    WCS_THROW("Invalid pointer to the reaction network.");
  }

  m_max_time = max_time;
  m_max_iter = max_iter;
  m_sim_time = static_cast<sim_time_t>(0);
  m_sim_iter = static_cast<sim_iter_t>(0u);

  { // initialize the random number generator
    if (rng_seed == 0u) {
      m_rgen_evt.set_seed();
      m_rgen_tm.set_seed();
    } else {
      seed_seq_param_t common_param_e
        = make_seed_seq_input(1, rng_seed, std::string("SSA_Direct"));
      seed_seq_param_t common_param_t
        = make_seed_seq_input(2, rng_seed, std::string("SSA_Direct"));

      std::vector<seed_seq_param_t> unique_params;
      const size_t num_procs = 1ul;
      const size_t my_rank = 0ul;

      // make sure to avoid generating any duplicate seed sequence
      gen_unique_seed_seq_params<rng_t::get_state_size()>(
          num_procs, common_param_e, unique_params);
      m_rgen_evt.use_seed_seq(unique_params[my_rank]);

      // make sure to avoid generating any duplicate seed sequence
      gen_unique_seed_seq_params<rng_t::get_state_size()>(
          num_procs, common_param_t, unique_params);
      m_rgen_tm.use_seed_seq(unique_params[my_rank]);
    }

    m_rgen_evt.param(typename rng_t::param_type(0.0, 1.0));
    m_rgen_tm.param(typename rng_t::param_type(0.0, 1.0));
  }

  Sim_Method::initialize_recording(m_net_ptr);

  build_propensity_list(); // prepare internal priority queue
 #if defined(WCS_HAS_ROSS)
  m_digests.emplace_back();
  m_digests.back().m_sim_time = m_sim_time;
 #endif // defined(WCS_HAS_ROSS)
}


void SSA_Direct::save_rgen_state(Sim_State_Change& digest)
{
  constexpr size_t rng_state_size = sizeof(m_rgen_evt.engine())
                                  + sizeof(m_rgen_tm.engine());
  digest.m_rng_state.clear();
  digest.m_rng_state.reserve(rng_state_size);
  wcs::ostreamvec<char> ostrmbuf(digest.m_rng_state);
  std::ostream os(&ostrmbuf);

 #if defined(WCS_HAS_CEREAL)
  cereal::BinaryOutputArchive oarchive(os);
  oarchive(m_rgen_evt.engine(), m_rgen_tm.engine());
 #else
  os << bits(m_rgen_evt.engine()) << bits(m_rgen_tm.engine());
 #endif // defined(WCS_HAS_CEREAL)
}


void SSA_Direct::load_rgen_state(const Sim_State_Change& digest)
{
  wcs::istreamvec<char> istrmbuf(digest.m_rng_state);
  std::istream is(&istrmbuf);

 #if defined(WCS_HAS_CEREAL)
  cereal::BinaryInputArchive iarchive(is);
  iarchive(m_rgen_evt.engine(), m_rgen_tm.engine());
 #else
  is >> bits(m_rgen_evt.engine()) >> bits(m_rgen_tm.engine());
 #endif // defined(WCS_HAS_CEREAL)
}


Sim_Method::result_t SSA_Direct::schedule(sim_time_t& next_time)
{
  if (BOOST_UNLIKELY(m_propensity.empty())) { // no reaction possible
    std::cerr << "No reaction exists." << std::endl;
    return Empty;
  }

  // Determine when the next reaction to occur
  const auto dt = get_reaction_time();
  next_time = m_sim_time + dt;

  if (BOOST_UNLIKELY((dt >= wcs::Network::get_etime_ulimit()) ||
                     (next_time > m_max_time))) {
    std::cerr << "No more reaction can fire." << std::endl;
    return Inactive;
  }

  return Success;
}


bool SSA_Direct::forward(const sim_time_t t)
{
  if (BOOST_UNLIKELY((m_sim_iter >= m_max_iter) || (t > m_max_time))) {
    return false; // do not continue simulation
  }
  ++ m_sim_iter;
  m_sim_time = t;

 #if defined(WCS_HAS_ROSS)
  m_digests.emplace_back();
  auto& digest = m_digests.back();
  // Backup RNG state before calling choose_reaction()
  save_rgen_state(digest);
 #else
  Sim_State_Change digest;
 #endif // defined(WCS_HAS_ROSS)

  // Determine the reaction to occur at this time
  auto& firing = choose_reaction();

  digest.m_sim_time = t;
  digest.m_reaction_fired = firing.second;

  // Execute the reaction, updating species counts
  Sim_Method::fire_reaction(digest);

  // Update the propensities of those reactions fired and affected
  update_reactions(firing, digest.m_reactions_affected, true);

 #if !defined(WCS_HAS_ROSS)
  // With ROSS, tracing and sampling are moved to process at commit time
  record(firing.second);
 #endif // defined(WCS_HAS_ROSS)

  return true;
}


#if defined(WCS_HAS_ROSS)
void SSA_Direct::backward(sim_time_t& t)
{
  // State of the last event to undo
  Sim_State_Change& digest = m_digests.back();
  // The BGL vertex descriptor of the the reaction to undo
  const auto& rd_fired = digest.m_reaction_fired;

  // Undo the species update done by the reaction fired
  undo_reaction(rd_fired);
  auto& fired = m_propensity.at(m_pindices.at(rd_fired));
  // Undo the propensity updates done for the reactions affected
  update_reactions(fired, digest.m_reactions_affected, false);

  // Restore the schedule
  t = digest.m_sim_time;
  // Restore the RNG state
  load_rgen_state(digest);
  // Free the state of the last event
  m_digests.pop_back();

  // Restore the current simulation time and iteration
  if (BOOST_UNLIKELY(m_digests.empty() ||
     (m_sim_iter == static_cast<sim_iter_t>(0)))) {
    WCS_THROW("Not able to schedule any reaction event!");
  } else {
    m_sim_time = m_digests.back().m_sim_time;
    m_sim_iter --;
  }
}


void SSA_Direct::record_first_n(const sim_iter_t num)
{
  if (m_digests.size() < 1ul) return;
  sim_iter_t i = static_cast<sim_iter_t>(0u);

  digest_list_t::iterator it = m_digests.begin();

  for (++it; it != m_digests.end(); ++it) {
    if (i >= num) break;
    record(it->m_sim_time, it->m_reaction_fired);
    i ++;
  }
  m_digests.erase(m_digests.begin(), --it);
}
#endif // defined(WCS_HAS_ROSS)


std::pair<sim_iter_t, sim_time_t> SSA_Direct::run()
{
  sim_time_t t = static_cast<sim_time_t>(0);

  if (schedule(t) != Success) {
    WCS_THROW("Not able to schedule any reaction event!");
  }

  while (BOOST_LIKELY(forward(t))) {
    if (BOOST_UNLIKELY(schedule(t) != Success)) {
      break;
    }

   /*
   #if defined(WCS_HAS_ROSS)
    { // rollback test
      backward(t);
      forward(t);
      schedule(t);
    }
   #endif // defined(WCS_HAS_ROSS)
   */
  }
 #if defined(WCS_HAS_ROSS)
  record_first_n(m_sim_iter);
 #endif // defined(WCS_HAS_ROSS)

  return std::make_pair(m_sim_iter, m_sim_time);
}

/**@}*/
} // end of namespace wcs
