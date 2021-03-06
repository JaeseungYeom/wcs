/******************************************************************************
 *                                                                            *
 *    Copyright 2020   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef __WCS_REACTION_NETWORK_NETWORK_HPP__
#define __WCS_REACTION_NETWORK_NETWORK_HPP__
// Whole Cell Model Simulator
/** @file
 * \ingroup wcs_reaction_network
 * \brief reaction graph representation.
 */
/** \ingroup wcs_reaction_network
 * \class wcs::Network
 * \brief reaction graph representation as a bipartite graph.
 *
 * Represent a reaction network as a bipartite graph using BGL.
 * A reaction graph consists of a set of species nodes, a set of reaction nodes,
 * and the directed edges between species and reactions.
 */

#include <string>
#include <unordered_map>
#include <tuple>
#include "bgl.hpp"
#include "reaction_network/species.hpp"
#include "reaction_network/reaction.hpp"
#include "reaction_network/vertex.hpp"
#include "reaction_network/edge.hpp"

namespace wcs {
/** \addtogroup wcs_reaction_network
 *  @{ */

class Network {
 public:
    /// The type of the BGL graph to represent reaction networks
  using graph_t  = boost::adjacency_list<
                   wcs_out_edge_list_t,
                   wcs_vertex_list_t,
                   boost::bidirectionalS,
                   wcs::Vertex, // vertex property bundle
                   wcs::Edge,   // edge property bundle
                   boost::no_property,
                   boost::vecS>;

  /// The type of the vertex property bundle
  using v_prop_t = boost::vertex_bundle_type<graph_t>::type;
  /// The type of BGL vertex descriptor for graph_t
  using v_desc_t = boost::graph_traits<graph_t>::vertex_descriptor;
  /// The type of BGL vertex iterator for graph_t
  using v_iter_t = boost::graph_traits<graph_t>::vertex_iterator;
  /// The type of the edge property bundle
  using e_prop_t = boost::edge_bundle_type<graph_t>::type;
  /// The type of BGL edge descriptor for graph_t
  using e_desc_t = boost::graph_traits<graph_t>::edge_descriptor;
  /// The type of BGL edge iterator for graph_t
  using e_iter_t = boost::graph_traits<graph_t>::edge_iterator;

  using vertex_type = v_prop_t::vertex_type;
  using v_label_t = std::string;

  /// Type of the species name
  using s_label_t = v_label_t;
  /// reaction driver type, std::pair<v_desc_t, stoic_t>
  using rdriver_t = Reaction<v_desc_t>::rdriver_t;
  /// Type of the map of species involve in a reaction
  using s_involved_t = std::map<s_label_t, rdriver_t>;
  /// Reaction property type
  using r_prop_t = wcs::Reaction<v_desc_t>;

  using rand_access
    = typename boost::detail::is_random_access<typename graph_t::vertex_list_selector>::type;

  /** The type of the list of species. This is chosen for the memory efficiency
    * and the lookup performance assuming an ordered container used to store
    * the graph */
  using map_idx2desc_t = std::vector<v_desc_t>;
  using reaction_list_t = map_idx2desc_t;
  using species_list_t  = map_idx2desc_t;

  using params_map_t = std::unordered_map <std::string, std::vector<std::string>>;
  using rate_rules_dep_t = std::unordered_map <std::string, std::set<std::string>>;

  /// Map a BGL vertex descriptor to the reaction index
  using map_desc2idx_t = std::unordered_map<v_desc_t, v_idx_t>;

 public:
  /** Load an input model file.
   *  We primarily support SBML as the formats of an input file. However,
   *  GraphML format is also allowed. The type of a file is automatically
   *  detected. With SBML, we may set to generate the reaction formula code.
   *  If that is the case and there already exists a library file generated
   *  in a previous run, by default, we skip the generation of a new library
   *  file and reuse the existing library file. Setting the second argument
   *  `reuse` to false forces regeneration.
   */
  void load(const std::string graphml_filename, const bool reuse = true);
  void init();
  void set_reaction_rate(const v_desc_t r, const reaction_rate_t rate) const;
  reaction_rate_t set_reaction_rate(const v_desc_t r) const;
  reaction_rate_t get_reaction_rate(const v_desc_t r) const;
  /** Computes reaction rate of every reaction `n' number of times and returns
    * total execution time */
  double compute_all_reaction_rates(const unsigned n = 1u) const;

  size_t get_num_vertices() const;
  size_t get_num_species() const;
  size_t get_num_reactions() const;
  size_t get_num_vertices_of_type(const vertex_type vt) const;

  /// Allow read-only access to the internal BGL graph
  const graph_t& graph() const;
  /// Allow read-only access to the internal reaction list
  const reaction_list_t& reaction_list() const;
  /// Allow read-only access to the internal species list
  const species_list_t& species_list() const;
  /// Find the species by the label and return the BGL vertex descriptor
  v_desc_t find_species(const std::string& label);
  /// Set the largest delay period for an active reaction to fire
  static void set_etime_ulimit(const sim_time_t t);
  /// Return the largest delay period for an active reaction to fire
  static sim_time_t get_etime_ulimit();

  bool check_reaction(const v_desc_t r) const;
  std::tuple<reaction_rate_t, reaction_rate_t, reaction_rate_t>
    find_min_max_rate() const;

  std::string show_species_labels(const std::string title="Species: ") const;
  std::string show_reaction_labels(const std::string title="Reaction:") const;
  std::string show_species_counts() const;
  std::string show_reaction_rates() const;

  const map_desc2idx_t& get_reaction_map() const;
  const map_desc2idx_t& get_species_map() const;

  v_idx_t reaction_d2i(v_desc_t d) const;
  v_desc_t reaction_i2d(v_idx_t i) const;
  v_idx_t species_d2i(v_desc_t d) const;
  v_desc_t species_i2d(v_idx_t i) const;

  /**
   * Set the partition id to each vertex (of both reaction and species types),
   * using the the list of partition ids ordered as the vertex descriptors
   * in the map from index to descriptor.
   * Then, make the list of local reactions, that belong to current partition,
   * specified as my_pid.
   */
  void set_partition(const map_idx2desc_t& idx2vd,
                     const std::vector<partition_id_t>& parts,
                     const partition_id_t my_pid);

  void set_partition(const std::vector<partition_id_t>& parts,
                     const partition_id_t my_pid);
  /**
   * Allow read-only access to the list of reactions that belong to this
   * partition.
   */
  const reaction_list_t& my_reaction_list() const;
  const reaction_list_t& my_species_list() const;
  /// Return the id of this partition
  partition_id_t get_partition_id() const;

  void print() const;

 protected:
  /// Sort the species list by the label (in lexicogrphical order)
  void sort_species();
  void build_index_maps();
  void loadGraphML(const std::string graphml_filename);
  void loadSBML(const std::string sbml_filename, const bool reuse = true);
  static void print_parameters_of_reactions(
             const params_map_t& dep_params_f,
             const params_map_t& dep_params_nf,
             const rate_rules_dep_t& rate_rules_dep_map);

 protected:
  /// The BGL graph to represent a reaction network
  graph_t m_graph;

  /// List of the BGL descriptors of reaction type vertices
  reaction_list_t m_reactions;

  /// List of the BGL descriptors of species type vertices
  species_list_t m_species;

  /// Map a BGL vertex descriptor to the reaction index
  map_desc2idx_t m_r_idx_map;

  /// Map a BGL vertex descriptor to the species index
  map_desc2idx_t m_s_idx_map;

  /**
   * The upper limit of the delay period for an active reaction to fire beyond
   * which we consider the reaction inactive/disabled. This is by default set to
   * the largest value of the sim_time_t type.
   */
  static sim_time_t m_etime_ulimit;

  /// Id of this partition. This is only relevant to parallel execution.
  partition_id_t m_pid;
  /// List of reactions that belong to this partition
  reaction_list_t m_my_reactions;
  /// List of species that belong to this partition
  species_list_t m_my_species;

 #if !defined(WCS_HAS_EXPRTK)
  /// all params in formula expected as input per reaction
  params_map_t m_dep_params_f;
  /// all params not in formula expected as input per reaction
  params_map_t m_dep_params_nf;
  /**
   *  all rate_rules (params in dep_params_f and dep_params_nf) with their
   *  dependent params (transient parameters)
   */
  rate_rules_dep_t m_rate_rules_dep_map;
 #endif // !defined(WCS_HAS_EXPRTK
};

/**@}*/
} // end of namespace wcs
#endif // __WCS_REACTION_NETWORK_NETWORK_HPP__
