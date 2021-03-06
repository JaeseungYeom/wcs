/******************************************************************************
 *                                                                            *
 *    Copyright 2020   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#if defined(WCS_HAS_CONFIG)
#include "wcs_config.hpp"
#else
#error "no config"
#endif

#include "utils/exception.hpp"
#include "utils/to_string.hpp"
#include "utils/file.hpp"
#include "utils/trajectory.hpp"

namespace wcs {
/** \addtogroup wcs_utils
 *  @{ */

Trajectory::Trajectory(const std::shared_ptr<wcs::Network>& net_ptr)
: m_net_ptr(net_ptr),
  m_frag_size(static_cast<frag_size_t>(0u)),
  m_cur_frag_id(static_cast<frag_id_t>(0u)),
  m_cur_record_in_frag(static_cast<frag_size_t>(0u)),
  m_num_steps(0u)
{
  if (!m_net_ptr) {
    WCS_THROW("Invaid pointer for reaction network.");
    return;
  }
  // The pointer net_ptr is a managed pointer and m_r_id_map is managed by the
  // object of the managed pointer. Thus, it keep it as raw.
  m_r_id_map = &(net_ptr->get_reaction_map());
  m_s_id_map = &(net_ptr->get_species_map());
}

Trajectory::~Trajectory()
{}

/**
 * Set output file name and specify how many record are written into each
 * fragment file.
 * Setting it to 0 will essentially turn off  fragment flushing by resetting
 * it to the number one less the maximum that can be represented by the type
 * of the variable.
 */
void Trajectory::set_outfile(const std::string outfile, const frag_size_t frag_size)
{
  std::string parent_dir;
  std::string stem;
  extract_file_component(outfile, parent_dir, stem, m_outfile_ext);
  m_outfile_stem = parent_dir + stem;
 #if defined(WCS_HAS_CEREAL)
  m_frag_size = (outfile.empty()? static_cast<frag_size_t>(0u) : frag_size);
 #else
  m_frag_size = static_cast<frag_size_t>(0u);
  if (frag_size > static_cast<frag_size_t>(0u)) {
    WCS_THROW("Need to build with the option WCS_WITH_CEREAL=ON " \
              "to use this feature.");
    return;
  }
 #endif // defined(WCS_HAS_CEREAL)

  if (m_frag_size == std::numeric_limits<frag_size_t>::max()) {
    WCS_THROW("Fragment size should be less than " +
              std::to_string(std::numeric_limits<frag_size_t>::max()));
    return;
  } else if (m_frag_size == static_cast<frag_size_t>(0u)) {
    m_frag_size = std::numeric_limits<frag_size_t>::max();
  }
}

void Trajectory::initialize()
{
  if (!m_net_ptr) {
    WCS_THROW("Invaid pointer for reaction network.");
    return;
  }
  record_initial_condition();
}

void Trajectory::record_initial_condition()
{
  const wcs::Network::graph_t& g = m_net_ptr->graph();

  size_t i = 0ul;
  m_species_counts.clear();
  m_species_counts.resize(m_net_ptr->get_num_species());

  for (const auto& vd : m_net_ptr->species_list()) {
    const auto& sv = g[vd]; // vertex (property) of the species
    // detailed vertex property data of the species
    const auto& sp = sv.property<s_prop_t>();
    m_species_counts[i++] = sp.get_count();
  }
}

void Trajectory::record_step(const sim_time_t t, const r_desc_t r)
{
  WCS_THROW("Base class does not implement this " \
            "'record_step(t, reaction)' method");
    return;
}

void Trajectory::record_step(const sim_time_t t, cnt_updates_t&& updates)
{
  WCS_THROW("Base class does not implement this " \
            "'record_step(t, cnt_updates)' method");
    return;
}

void Trajectory::record_step(const sim_time_t t, conc_updates_t&& updates)
{
  WCS_THROW("Base class does not implement this " \
            "'record_step(t, conc_updates)' method");
    return;
}

void Trajectory::flush()
{
  m_cur_record_in_frag = static_cast<frag_size_t>(0u);
}

/**@}*/
} // end of namespace wcs
