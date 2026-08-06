// #include <fstream>
#include <exploration_manager/c2_exploration_manager.h>
#include <thread>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <limits>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <cstdlib>
#include <iomanip>

#include <active_perception/graph_node.h>
#include <active_perception/perception_utils.h>
#include <active_perception/frontier_finder.h>
#include <active_perception/hgrid.h>

#include <plan_env/raycast.h>
#include <plan_env/sdf_map.h>
#include <plan_env/edt_environment.h>
#include <plan_env/communication_graph.h>
#include <plan_env/multi_map_manager.h>

#include <plan_manage/planner_manager.h>
#include <path_searching/astar2.h>

#include <lkh_tsp_solver/SolveTSP.h>
#include <lkh_mtsp_solver/SolveMTSP.h>

#include <exploration_manager/expl_data.h>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <visualization_msgs/Marker.h>

using namespace Eigen;

namespace c2_expl {

C2ExplorationManager::C2ExplorationManager() {
}

C2ExplorationManager::~C2ExplorationManager() {
  ViewNode::astar_.reset();
  ViewNode::caster_.reset();
  ViewNode::map_.reset();
}

void C2ExplorationManager::logTelemetryEvent(
    const std::string& event, const std::string& fields) const {
  if (telemetry_path_.empty()) return;
  std::ofstream out(telemetry_path_, std::ios::app);
  if (!out.is_open()) return;
  const int drone_id = ep_ ? ep_->drone_id_ : -1;
  out << std::setprecision(17)
      << "{\"wall_time_s\":" << ros::WallTime::now().toSec()
      << ",\"ros_time_s\":" << ros::Time::now().toSec()
      << ",\"drone_id\":" << drone_id << ",\"event\":\"" << event << "\"";
  if (!fields.empty()) out << "," << fields;
  out << "}\n";
}

std::string C2ExplorationManager::telemetryTaskFields(const Vector3d& target_pos) const {
  std::ostringstream fields;
  fields << std::setprecision(17);

  int matched_center_id = -1;
  int matched_grid_id = -1;
  int matched_center_type = -1;
  double matched_distance = std::numeric_limits<double>::infinity();
  bool matched_from_state = false;

  // Prefer the current drone's assigned state, because the last cost matrix may already
  // belong to a later allocation round. This is a nearest-neighbor identity check only.
  if (ed_ && ep_ && ep_->drone_id_ > 0 &&
      ep_->drone_id_ <= static_cast<int>(ed_->swarm_state_.size())) {
    const auto& state = ed_->swarm_state_[ep_->drone_id_ - 1];
    for (size_t i = 0; i < state.center_positions_.size(); ++i) {
      const double distance = (state.center_positions_[i] - target_pos).norm();
      if (distance < matched_distance && distance <= 1.0) {
        matched_distance = distance;
        matched_center_id = i < state.center_ids_.size() ? state.center_ids_[i] : -1;
        matched_grid_id = i < state.center_grid_ids_.size() ? state.center_grid_ids_[i] : -1;
        matched_from_state = true;
      }
    }
  }

  if (!matched_from_state && hgrid_) {
    int center_index = -1;
    if (hgrid_->findCenterIndexByPos(target_pos, center_index, 1.0)) {
      const auto& positions = hgrid_->getLastCostMatrixCenterPositions();
      if (center_index >= 0 && center_index < static_cast<int>(positions.size())) {
        matched_distance = (positions[center_index] - target_pos).norm();
        const auto& ids = hgrid_->getLastCostMatrixCenterIds();
        const auto& grids = hgrid_->getLastCostMatrixCenterGridIds();
        const auto& types = hgrid_->getLastCostMatrixCenterTypes();
        if (center_index < static_cast<int>(ids.size())) matched_center_id = ids[center_index];
        if (center_index < static_cast<int>(grids.size())) matched_grid_id = grids[center_index];
        if (center_index < static_cast<int>(types.size())) matched_center_type = types[center_index];
      }
    }
  }

  if (matched_center_id >= 0 || matched_grid_id >= 0) {
    fields << "\"task_match_kind\":\"assigned_center\""
           << ",\"task_center_id\":" << matched_center_id
           << ",\"task_grid_id\":" << matched_grid_id
           << ",\"task_center_type\":" << matched_center_type
           << ",\"task_match_distance_m\":" << matched_distance;
  } else {
    int grid_id = -1;
    if (hgrid_ && hgrid_->getGridIdByCenterPos(target_pos, grid_id, 1.0)) matched_grid_id = grid_id;
    fields << "\"task_match_kind\":\"unmatched_frontier\""
           << ",\"task_center_id\":-1"
           << ",\"task_grid_id\":" << matched_grid_id
           << ",\"task_center_type\":-1"
           << ",\"task_match_distance_m\":null";
  }
  fields << ",\"target_x\":" << target_pos.x()
         << ",\"target_y\":" << target_pos.y()
         << ",\"target_z\":" << target_pos.z();
  return fields.str();
}

void C2ExplorationManager::snapshotTelemetryFile(
    const std::string& source_path, const std::string& snapshot_name) const {
  if (telemetry_dir_.empty()) return;
  std::ifstream source(source_path, std::ios::binary);
  if (!source.is_open()) return;
  std::ofstream target(telemetry_dir_ + "/" + snapshot_name, std::ios::binary | std::ios::trunc);
  if (!target.is_open()) return;
  target << source.rdbuf();
}

bool C2ExplorationManager::isPrctTargetCooled(
    const int frontier_id, const Vector3d& goal) const {
  if (frontier_id < 0) return false;
  if (prct_enable_retry_suppression_) {
    const double now_wall_s = ros::WallTime::now().toSec();
    const bool quarantine_mode = prct_backoff_enabled_;
    const auto fit = prct_frontier_cooldowns_.find(frontier_id);
    if (fit != prct_frontier_cooldowns_.end()) {
      if (fit->second.map_version == currentMapVersion() &&
          fit->second.repeat_count >= prct_repeat_threshold_ &&
          (quarantine_mode || now_wall_s < fit->second.cooldown_until_wall_s)) {
        return true;
      }
    }
    const auto it = prct_cooldowns_.find(
        prctCooldownKey(frontier_id, goal, currentMapVersion()));
    if (it != prct_cooldowns_.end()) {
      if (it->second.repeat_count >= prct_repeat_threshold_ &&
          (quarantine_mode || now_wall_s < it->second.cooldown_until_wall_s)) {
        return true;
      }
    }
  }
  return c3MarginalGateActive() && isPrctTakeoverCooled(frontier_id, goal);
}

bool C2ExplorationManager::shouldSuppressPrctTarget(
    const int frontier_id, const Vector3d& goal) const {
  return isPrctTargetCooled(frontier_id, goal);
}

std::vector<int> C2ExplorationManager::prctFilterCooledTargets(
    const std::vector<int>& frontier_ids) const {
  if (frontier_ids.empty()) return frontier_ids;
  const bool c3_active = c3MarginalGateActive();
  if (!c3_active && !prct_enable_retry_suppression_) return frontier_ids;
  std::vector<int> kept;
  kept.reserve(frontier_ids.size());
  for (const int id : frontier_ids) {
    const bool cooled =
        id >= 0 && id < static_cast<int>(ed_->points_.size()) &&
        isPrctTargetCooled(id, ed_->points_[id]);
    if (cooled) {
      std::ostringstream fields;
      fields << std::setprecision(17)
             << "\"frontier_id\":" << id
             << ",\"quarantine_enabled\":" << (prct_backoff_enabled_ ? 1 : 0)
             << ",\"goal_x\":" << ed_->points_[id].x()
             << ",\"goal_y\":" << ed_->points_[id].y()
             << ",\"goal_z\":" << ed_->points_[id].z()
             << ",\"map_epoch\":" << currentMapVersion();
      logTelemetryEvent("prct_retry_suppression_skip", fields.str());
      continue;
    }
    kept.push_back(id);
  }
  return kept;
}

double C2ExplorationManager::prctFailureCooldownS(const int repeat_count) const {
  (void) repeat_count;
  if (prct_backoff_enabled_) return std::numeric_limits<double>::max();
  return prct_cooldown_s_;
}

void C2ExplorationManager::registerPrctFailure(
    const int frontier_id, const Vector3d& goal) {
  if (frontier_id < 0) return;
  const uint64_t map_version = currentMapVersion();
  const std::string key = prctCooldownKey(frontier_id, goal, map_version);
  const bool c3_active = c3MarginalGateActive();
  if (c3_active) {
    if (prct_enable_retry_suppression_) {
      auto& entry = prct_cooldowns_[key];
      ++entry.repeat_count;
      entry.map_version = map_version;
      const double backoff_s = prctFailureCooldownS(entry.repeat_count);
      const double now_wall_s = ros::WallTime::now().toSec();
      entry.cooldown_until_wall_s =
          prct_backoff_enabled_ ? std::numeric_limits<double>::max()
                                : now_wall_s + backoff_s;
      const double display_backoff_s = prct_backoff_enabled_ ? -1.0 : backoff_s;
      std::ostringstream suppress_fields;
      suppress_fields << std::setprecision(17)
             << "\"frontier_id\":" << frontier_id
             << ",\"map_version\":" << map_version
             << ",\"repeat_count\":" << entry.repeat_count
             << ",\"backoff_s\":" << display_backoff_s
             << ",\"quarantine_enabled\":" << (prct_backoff_enabled_ ? 1 : 0)
             << ",\"backoff_enabled\":" << (prct_backoff_enabled_ ? 1 : 0)
             << ",\"cooldown_until_wall_s\":" << entry.cooldown_until_wall_s
             << ",\"goal_x\":" << goal.x()
             << ",\"goal_y\":" << goal.y()
             << ",\"goal_z\":" << goal.z();
      logTelemetryEvent("prct_retry_suppression_register", suppress_fields.str());
      auto& frontier_entry = prct_frontier_cooldowns_[frontier_id];
      ++frontier_entry.repeat_count;
      frontier_entry.map_version = map_version;
      frontier_entry.cooldown_until_wall_s =
          prct_backoff_enabled_ ? std::numeric_limits<double>::max()
                                : ros::WallTime::now().toSec() +
                                      prctFailureCooldownS(frontier_entry.repeat_count);
    }
    int& repeat_count = c3_failure_repeat_counts_[key];
    if (repeat_count == 0) {
      c3_failure_chain_start_wall_s_[key] = ros::WallTime::now().toSec();
    }
    ++repeat_count;
    std::ostringstream fields;
    fields << std::setprecision(17)
           << "\"frontier_id\":\"" << frontier_id
           << "\",\"map_version\":\"" << map_version
           << "\",\"repeat_count\":\"" << repeat_count
           << "\",\"chain_start_wall_s\":\""
           << c3_failure_chain_start_wall_s_[key]
           << "\",\"goal_x\":\"" << goal.x()
           << "\",\"goal_y\":\"" << goal.y()
           << "\",\"goal_z\":\"" << goal.z() << "\"";
    logTelemetryEvent("c3_failure_repeat_register", fields.str());
    return;
  }
  if (!prct_enable_retry_suppression_) return;
  auto& entry = prct_cooldowns_[key];
  ++entry.repeat_count;
  entry.map_version = map_version;
  const double backoff_s = prctFailureCooldownS(entry.repeat_count);
  const double now_wall_s = ros::WallTime::now().toSec();
  entry.cooldown_until_wall_s =
      prct_backoff_enabled_ ? std::numeric_limits<double>::max()
                            : now_wall_s + backoff_s;
  const double display_backoff_s = prct_backoff_enabled_ ? -1.0 : backoff_s;
  std::ostringstream fields;
  fields << std::setprecision(17)
         << "\"frontier_id\":" << frontier_id
         << ",\"map_version\":" << map_version
         << ",\"repeat_count\":" << entry.repeat_count
         << ",\"backoff_s\":" << display_backoff_s
         << ",\"quarantine_enabled\":" << (prct_backoff_enabled_ ? 1 : 0)
         << ",\"backoff_enabled\":" << (prct_backoff_enabled_ ? 1 : 0)
         << ",\"cooldown_until_wall_s\":" << entry.cooldown_until_wall_s
         << ",\"goal_x\":" << goal.x()
        << ",\"goal_y\":" << goal.y()
        << ",\"goal_z\":" << goal.z();
  logTelemetryEvent("prct_retry_suppression_register", fields.str());
  auto& frontier_entry = prct_frontier_cooldowns_[frontier_id];
  ++frontier_entry.repeat_count;
  frontier_entry.map_version = map_version;
  frontier_entry.cooldown_until_wall_s =
      prct_backoff_enabled_ ? std::numeric_limits<double>::max()
                            : ros::WallTime::now().toSec() +
                                      prctFailureCooldownS(frontier_entry.repeat_count);
}

void C2ExplorationManager::registerPrctSuccess(
    const int frontier_id, const Vector3d& goal) {
  if (frontier_id < 0 || !prct_enable_retry_suppression_) return;
  const uint64_t map_version = currentMapVersion();
  bool changed = false;
  const std::string key = prctCooldownKey(frontier_id, goal, map_version);
  auto it = prct_cooldowns_.find(key);
  if (it != prct_cooldowns_.end() && it->second.map_version == map_version) {
    if (it->second.repeat_count > 0 || it->second.cooldown_until_wall_s > 0.0) {
      it->second.repeat_count = 0;
      it->second.cooldown_until_wall_s = 0.0;
      changed = true;
    }
  }
  auto fit = prct_frontier_cooldowns_.find(frontier_id);
  if (fit != prct_frontier_cooldowns_.end() && fit->second.map_version == map_version) {
    if (fit->second.repeat_count > 0 || fit->second.cooldown_until_wall_s > 0.0) {
      fit->second.repeat_count = 0;
      fit->second.cooldown_until_wall_s = 0.0;
      changed = true;
    }
  }
  if (changed) {
    std::ostringstream fields;
    fields << std::setprecision(17)
           << "\"frontier_id\":" << frontier_id
           << ",\"map_version\":" << map_version
           << ",\"goal_x\":" << goal.x()
           << ",\"goal_y\":" << goal.y()
           << ",\"goal_z\":" << goal.z();
    logTelemetryEvent("prct_failure_chain_reset", fields.str());
  }
}

std::string C2ExplorationManager::prctCooldownKey(
    const int frontier_id, const Vector3d& goal, const uint64_t map_version) const {
  std::ostringstream key;
  key << "f=" << frontier_id
      << ",g=" << std::fixed << std::setprecision(1)
      << std::round(goal.x() * 10.0) / 10.0 << ","
      << std::round(goal.y() * 10.0) / 10.0 << ","
      << std::round(goal.z() * 10.0) / 10.0
      << ",m=" << map_version
      << ",o=" << (ep_ ? ep_->drone_id_ : -1);
  return key.str();
}

uint64_t C2ExplorationManager::currentMapVersion() const {
  return prct_map_epoch_;
}

void C2ExplorationManager::initialize(ros::NodeHandle& nh) {
  planner_manager_.reset(new FastPlannerManager);
  planner_manager_->initPlanModules(nh);

  edt_environment_ = planner_manager_->edt_environment_;
  sdf_map_ = edt_environment_->sdf_map_;

  // Initialize communication graph with environment
  comm_graph_.reset(new CommunicationGraph(edt_environment_, nh));

  // Set communication graph directly to multi-map manager
  if (sdf_map_->getMultiMapManager()) {
    sdf_map_->getMultiMapManager()->setCommunicationGraph(comm_graph_);
  }

  frontier_finder_.reset(new FrontierFinder(edt_environment_, nh));
  // uniform_grid_.reset(new UniformGrid(edt_environment_, nh));
  hgrid_.reset(new HGrid(edt_environment_, nh));
  // view_finder_.reset(new ViewFinder(edt_environment_, nh));

  ed_.reset(new ExplorationData);
  ep_.reset(new ExplorationParam);

  nh.param("exploration/refine_local", ep_->refine_local_, true);
  nh.param("exploration/refined_num", ep_->refined_num_, -1);
  nh.param("exploration/refined_radius", ep_->refined_radius_, -1.0);
  nh.param("exploration/top_view_num", ep_->top_view_num_, -1);
  nh.param("exploration/max_decay", ep_->max_decay_, -1.0);
  nh.param("exploration/tsp_dir", ep_->tsp_dir_, string("null"));
  nh.param("exploration/mtsp_dir", ep_->mtsp_dir_, string("null"));
  nh.param("exploration/relax_time", ep_->relax_time_, 1.0);
  nh.param("exploration/drone_num", ep_->drone_num_, 1);
  nh.param("exploration/drone_id", ep_->drone_id_, 1);
  nh.param("exploration/init_plan_num", ep_->init_plan_num_, 2);
  nh.param("exploration/prob_type", ep_->prob_type_, 1);
  nh.param("exploration/center_switch_penalty", ep_->center_switch_penalty_, 0.0);
  nh.param("exploration/center_stay_bonus", ep_->center_stay_bonus_, 0.0);
  nh.param("exploration/reachability_shadow_max_candidates", reachability_shadow_max_candidates_, 0);
  nh.param("exploration/reachability_peer_shadow_max_peers", reachability_peer_shadow_max_peers_, 0);
  nh.param("exploration/prct_enable_retry_suppression", prct_enable_retry_suppression_, false);
  nh.param("exploration/prct_backoff_enabled", prct_backoff_enabled_, false);
  nh.param("exploration/prct_repeat_threshold", prct_repeat_threshold_, 3);
  nh.param("exploration/prct_cooldown_s", prct_cooldown_s_, 5.0);
  nh.param("exploration/prct_backoff_initial_s", prct_backoff_initial_s_, 5.0);
  nh.param("exploration/prct_backoff_max_s", prct_backoff_max_s_, 30.0);
  nh.param("exploration/prct_backoff_factor", prct_backoff_factor_, 2.0);
  ros::param::param("/prct_backoff_initial_s", prct_backoff_initial_s_, prct_backoff_initial_s_);
  ros::param::param("/prct_backoff_max_s", prct_backoff_max_s_, prct_backoff_max_s_);
  ros::param::param("/prct_backoff_factor", prct_backoff_factor_, prct_backoff_factor_);
  ros::param::param("/prct_enable_retry_suppression", prct_enable_retry_suppression_,
      prct_enable_retry_suppression_);
  ros::param::param("/prct_backoff_enabled", prct_backoff_enabled_, prct_backoff_enabled_);
  ros::param::param("/prct_repeat_threshold", prct_repeat_threshold_,
      prct_repeat_threshold_);
  ros::param::param("/prct_cooldown_s", prct_cooldown_s_, prct_cooldown_s_);

  const char* telemetry_dir = std::getenv("ECRTA_TELEMETRY_DIR");
  if (telemetry_dir != nullptr && telemetry_dir[0] != '\0') {
    telemetry_dir_ = telemetry_dir;
    telemetry_path_ = std::string(telemetry_dir) + "/telemetry_drone_" +
        std::to_string(ep_->drone_id_) + ".jsonl";
  }
  logTelemetryEvent("initialize", "\"drone_num\":" + std::to_string(ep_->drone_num_));

  ed_->swarm_state_.resize(ep_->drone_num_);
  ed_->meeting_opt_stamps_.resize(ep_->drone_num_);
  ed_->meeting_opt_res_stamps_.resize(ep_->drone_num_);
  ed_->meeting_opt_commit_res_stamps_.resize(ep_->drone_num_);
  ed_->opt_peer_backoff_until_.resize(ep_->drone_num_);
  ed_->opt_peer_reject_streak_.resize(ep_->drone_num_);
  for (int i = 0; i < ep_->drone_num_; ++i) {
    ed_->swarm_state_[i].stamp_ = 0.0;
    ed_->swarm_state_[i].recv_stamp_ = 0.0;
    ed_->meeting_opt_stamps_[i] = 0.0;
    ed_->meeting_opt_res_stamps_[i] = 0.0;
    ed_->meeting_opt_commit_res_stamps_[i] = 0.0;
    ed_->opt_peer_backoff_until_[i] = 0.0;
    ed_->opt_peer_reject_streak_[i] = 0;
  }
  planner_manager_->swarm_traj_data_.init(ep_->drone_id_, ep_->drone_num_);

  nh.param("exploration/vm", ViewNode::vm_, -1.0);
  nh.param("exploration/am", ViewNode::am_, -1.0);
  nh.param("exploration/yd", ViewNode::yd_, -1.0);
  nh.param("exploration/ydd", ViewNode::ydd_, -1.0);
  nh.param("exploration/w_dir", ViewNode::w_dir_, -1.0);

  ViewNode::astar_.reset(new Astar);
  ViewNode::astar_->init(nh, edt_environment_);
  ViewNode::map_ = sdf_map_;

  double resolution_ = sdf_map_->getResolution();
  Eigen::Vector3d origin, size;
  sdf_map_->getRegion(origin, size);
  ViewNode::caster_.reset(new RayCaster);
  ViewNode::caster_->setParams(resolution_, origin);

  planner_manager_->path_finder_->lambda_heu_ = 1.0;
  // planner_manager_->path_finder_->max_search_time_ = 0.05;
  planner_manager_->path_finder_->max_search_time_ = 1.0;

  tsp_client_ =
      nh.serviceClient<lkh_mtsp_solver::SolveMTSP>("/solve_tsp_" + to_string(ep_->drone_id_), true);
  acvrp_client_ = nh.serviceClient<lkh_mtsp_solver::SolveMTSP>(
      "/solve_acvrp_" + to_string(ep_->drone_id_), true);

  peer_reachability_query_pub_ =
      nh.advertise<exploration_manager::PeerReachabilityQuery>(
          "/peer_reachability_query", 10);
  peer_reachability_query_sub_ =
      nh.subscribe("/peer_reachability_query", 10,
                   &C2ExplorationManager::peerReachabilityQueryCallback, this);
  peer_reachability_result_pub_ =
      nh.advertise<exploration_manager::PeerReachabilityResult>(
          "/peer_reachability_result", 10);
  peer_reachability_result_sub_ =
      nh.subscribe("/peer_reachability_result", 10,
                   &C2ExplorationManager::peerReachabilityResultCallback, this);
  nh.param("exploration/peer_takeover_goal_timeout_s", peer_takeover_goal_timeout_s_, 8.0);
  nh.param("exploration/peer_takeover_goal_max_queue", peer_takeover_goal_max_queue_, 3);
  nh.param("exploration/peer_handoff_wait_s", peer_handoff_wait_s_, 8.0);
  nh.param("exploration/prct_enable_peer_takeover", prct_enable_peer_takeover_, false);
  nh.param("exploration/prct_peer_cert_wait_s", prct_peer_cert_wait_s_, 0.25);
  nh.param("exploration/prct_peer_handoff_timeout_s", prct_peer_handoff_timeout_s_, 2.0);
  nh.param("exploration/prct_peer_state_max_age_s", prct_peer_state_max_age_s_, 2.0);
  ros::param::param("/prct_enable_peer_takeover", prct_enable_peer_takeover_,
      prct_enable_peer_takeover_);
  ros::param::param("/prct_peer_cert_wait_s", prct_peer_cert_wait_s_,
      prct_peer_cert_wait_s_);
  ros::param::param("/prct_peer_handoff_timeout_s", prct_peer_handoff_timeout_s_,
      prct_peer_handoff_timeout_s_);
  ros::param::param("/prct_peer_state_max_age_s", prct_peer_state_max_age_s_,
      prct_peer_state_max_age_s_);
  nh.param("exploration/c3_enable_marginal_gate", c3_enable_marginal_gate_, true);
  ros::param::param("/c3_enable_marginal_gate", c3_enable_marginal_gate_, c3_enable_marginal_gate_);
  nh.param("exploration/c3_benefit_margin_s", c3_benefit_margin_s_, 1.0);
  ros::param::param("/c3_benefit_margin_s", c3_benefit_margin_s_, c3_benefit_margin_s_);
  nh.param("exploration/c3_trust_threshold", c3_trust_threshold_, 0.5);
  ros::param::param("/c3_trust_threshold", c3_trust_threshold_, c3_trust_threshold_);
  nh.param("exploration/c3_load_weight", c3_load_weight_, 0.5);
  ros::param::param("/c3_load_weight", c3_load_weight_, c3_load_weight_);
  nh.param("exploration/c3_handoff_overhead_s", c3_handoff_overhead_s_, 0.5);
  ros::param::param("/c3_handoff_overhead_s", c3_handoff_overhead_s_, c3_handoff_overhead_s_);
  nh.param("exploration/c3_trust_penalty_s", c3_trust_penalty_s_, 2.0);
  ros::param::param("/c3_trust_penalty_s", c3_trust_penalty_s_, c3_trust_penalty_s_);
  nh.param("exploration/c3_nominal_speed_m_s", c3_nominal_speed_m_s_, 2.0);
  ros::param::param("/c3_nominal_speed_m_s", c3_nominal_speed_m_s_, c3_nominal_speed_m_s_);
  nh.param("exploration/c3_owner_fallback_penalty_s", c3_owner_fallback_penalty_s_, 3.0);
  ros::param::param("/c3_owner_fallback_penalty_s", c3_owner_fallback_penalty_s_, c3_owner_fallback_penalty_s_);
  nh.param("exploration/c3_owner_stuck_alpha", c3_owner_stuck_alpha_, 1.0);
  ros::param::param("/c3_owner_stuck_alpha", c3_owner_stuck_alpha_, c3_owner_stuck_alpha_);
  nh.param("exploration/c3_min_repeat_count", c3_min_repeat_count_, 3);
  ros::param::param("/c3_min_repeat_count", c3_min_repeat_count_, c3_min_repeat_count_);
  nh.param("exploration/c3_owner_repeat_cost_s", c3_owner_repeat_cost_s_, 0.3);
  ros::param::param("/c3_owner_repeat_cost_s", c3_owner_repeat_cost_s_, c3_owner_repeat_cost_s_);
  nh.param("exploration/c3_peer_cert_grace_s", c3_peer_cert_grace_s_, 0.6);
  ros::param::param("/c3_peer_cert_grace_s", c3_peer_cert_grace_s_, c3_peer_cert_grace_s_);
  nh.param("exploration/c3_takeover_cooldown_s", c3_takeover_cooldown_s_, 30.0);
  ros::param::param("/c3_takeover_cooldown_s", c3_takeover_cooldown_s_, c3_takeover_cooldown_s_);
  nh.param("exploration/c3_max_takeover_attempts", c3_max_takeover_attempts_, 3);
  ros::param::param("/c3_max_takeover_attempts", c3_max_takeover_attempts_, c3_max_takeover_attempts_);
  nh.param("exploration/c3_takeover_completed_cooldown_s", c3_takeover_completed_cooldown_s_, 120.0);
  ros::param::param("/c3_takeover_completed_cooldown_s", c3_takeover_completed_cooldown_s_,
      c3_takeover_completed_cooldown_s_);
  peer_takeover_goal_pub_ =
      nh.advertise<exploration_manager::PeerTakeoverGoal>("/peer_takeover_goal", 10);
  peer_takeover_goal_sub_ =
      nh.subscribe("/peer_takeover_goal", 10,
                   &C2ExplorationManager::peerTakeoverGoalCallback, this);
  peer_takeover_receipt_pub_ =
      nh.advertise<exploration_manager::PeerTakeoverReceipt>("/peer_takeover_receipt", 10);
  peer_takeover_receipt_sub_ =
      nh.subscribe("/peer_takeover_receipt", 10,
                   &C2ExplorationManager::peerTakeoverReceiptCallback, this);
  pending_peer_takeover_goals_.clear();
  peer_handoff_pending_ = false;
  peer_handoff_published_ = false;
  peer_handoff_observed_ = false;
  peer_handoff_best_peer_id_ = -1;
  peer_handoff_publish_count_ = 0;
  peer_handoff_deadline_wall_s_ = 0.0;
  peer_handoff_cert_window_end_wall_s_ = 0.0;
  peer_handoff_request_id_ = 0;
  pending_peer_certificates_.clear();
  peer_handoff_receipt_status_.clear();
  peer_handoff_fallback_reason_.clear();

  // Swarm
  for (auto& state : ed_->swarm_state_) {
    state.stamp_ = 0.0;
    state.recv_stamp_ = 0.0;
    state.recent_interact_time_ = 0.0;
    state.recent_attempt_time_ = 0.0;
  }
  ed_->last_grid_ids_ = {};
  ed_->reallocated_ = true;
  ed_->meeting_opt_stamp_ = 0.0;
  ed_->opt_wait_start_time_ = 0.0;
  ed_->opt_last_send_time_ = 0.0;
  ed_->opt_retry_count_ = 0;
  ed_->opt_commit_wait_start_time_ = 0.0;
  ed_->opt_commit_last_send_time_ = 0.0;
  ed_->opt_commit_retry_count_ = 0;
  ed_->opt_preferred_gate_host_id_ = -1;
  ed_->opt_preferred_gate_since_ = 0.0;
  ed_->opt_pending_host_id_ = -1;
  ed_->opt_pending_stamp_ = 0.0;
  ed_->opt_pending_recv_time_ = 0.0;
  ed_->opt_commit_apply_pending_ = false;
  ed_->opt_commit_apply_host_id_ = -1;
  ed_->opt_commit_apply_stamp_ = 0.0;
  ed_->opt_commit_apply_time_ = 0.0;
  ed_->opt_commit_backup_valid_ = false;
  ed_->wait_opt_response_ = false;
  ed_->wait_opt_commit_ack_ = false;
  ed_->plan_num_ = 0;
}

int C2ExplorationManager::planExploreMotion(
    const Vector3d& pos, const Vector3d& vel, const Vector3d& acc, const Vector3d& yaw) {
  telemetry_active_frontier_candidates_.clear();
  telemetry_active_local_view_candidates_.clear();
  logTelemetryEvent("explore_request",
      "\"frontier_count_before\":" + std::to_string(ed_->frontiers_.size()) +
          ",\"grid_assignment_count\":" + std::to_string(ed_->last_grid_ids_.size()) +
          ",\"pos_x\":" + std::to_string(pos.x()) + ",\"pos_y\":" +
          std::to_string(pos.y()) + ",\"pos_z\":" + std::to_string(pos.z()));
  ros::Time t1 = ros::Time::now();
  auto t2 = t1;

  if (hasPendingPeerHandoff()) {
    logTelemetryEvent("explore_result",
        "\"success\":false,\"reason\":\"peer_handoff_wait\"");
    return HANDOFF;
  }

  // Do global and local tour planning and retrieve the next viewpoint

  ed_->frontier_tour_.clear();
  Vector3d next_pos;
  double next_yaw;
  int selected_frontier_id = -1;
  const char* target_source = "unselected";
  vector<int> grid_ids, frontier_ids;
  std::uint64_t takeover_owner_plan_seq = 0;
  std::uint8_t takeover_owner_drone_id = 0;
  std::uint64_t takeover_request_id = 0;
  double takeover_issued_wall_s = 0.0;
  if (takeNextPeerTakeoverGoal(next_pos, next_yaw, selected_frontier_id,
                               takeover_owner_plan_seq, takeover_owner_drone_id,
                               takeover_request_id, takeover_issued_wall_s)) {
    target_source = "peer_takeover";
    ed_->next_pos_ = next_pos;
    ed_->next_yaw_ = next_yaw;
    telemetry_active_frontier_id_ = selected_frontier_id;
    logTelemetryEvent("target_selection",
        "\"allocation_seq\":" + std::to_string(telemetry_active_allocation_seq_) + "," +
            telemetryTaskFields(next_pos) + ",\"frontier_id\":" +
            std::to_string(telemetry_active_frontier_id_) +
            ",\"expected_plan_seq\":" + std::to_string(telemetry_plan_seq_ + 1) +
            ",\"target_source\":\"peer_takeover\",\"assigned_grid_first\":-1"
            ",\"grid_candidate_count\":0,\"frontier_candidate_count\":1"
            ",\"local_view_candidate_count\":0,\"selected_frontier_id\":" +
            std::to_string(selected_frontier_id) +
            ",\"goal_x\":" + std::to_string(next_pos.x()) + ",\"goal_y\":" +
            std::to_string(next_pos.y()) + ",\"goal_z\":" + std::to_string(next_pos.z()));
    if (planTrajToView(pos, vel, acc, yaw, next_pos, next_yaw) == FAIL) {
      sendPeerTakeoverReceipt(takeover_owner_drone_id, takeover_request_id,
          takeover_owner_plan_seq, selected_frontier_id, next_pos, next_yaw,
          takeover_issued_wall_s, TAKEOVER_ABORTED, "ABORTED", "trajectory_fail");
      logTelemetryEvent("explore_result", "\"success\":false,\"reason\":\"trajectory_fail\"");
      return FAIL;
    }
    sendPeerTakeoverReceipt(takeover_owner_drone_id, takeover_request_id,
        takeover_owner_plan_seq, selected_frontier_id, next_pos, next_yaw,
        takeover_issued_wall_s, TAKEOVER_COMPLETED, "COMPLETED", "planned");
    logTelemetryEvent("peer_takeover_goal_executed",
        "\"owner_plan_seq\":" + std::to_string(takeover_owner_plan_seq) +
            ",\"request_id\":" + std::to_string(takeover_request_id) +
            ",\"frontier_id\":" + std::to_string(selected_frontier_id));
    logTelemetryEvent("explore_result",
        "\"success\":true,\"grid_count\":0,\"frontier_count\":1"
        ",\"goal_x\":" + std::to_string(next_pos.x()) + ",\"goal_y\":" +
        std::to_string(next_pos.y()) + ",\"goal_z\":" + std::to_string(next_pos.z()) +
        ",\"planning_total_s\":" + std::to_string((ros::Time::now() - t2).toSec()));
    return SUCCEED;
  }
  findGridAndFrontierPath(pos, vel, yaw, grid_ids, frontier_ids);
  telemetry_active_frontier_candidates_ = frontier_ids;
  const std::vector<int> raw_frontier_ids = frontier_ids;
  frontier_ids = prctFilterCooledTargets(frontier_ids);
  if (frontier_ids.size() != raw_frontier_ids.size()) {
    logTelemetryEvent("prct_candidate_filter",
        "\"raw_frontier_count\":" + std::to_string(raw_frontier_ids.size()) +
            ",\"kept_frontier_count\":" + std::to_string(frontier_ids.size()));
  }

  if (grid_ids.empty()) {
    logTelemetryEvent("explore_result", "\"success\":false,\"reason\":\"no_grid\"");
    return NO_GRID;

  } else if (frontier_ids.size() == 0) {
    // Detect whether current task token is hull-based (for logging and fallback behavior).
    bool next_task_is_hull = false;
    const auto& ego_state = ed_->swarm_state_[ep_->drone_id_ - 1];
    if (ed_->region_tour_.size() > 1 && !ego_state.center_positions_.empty()) {
      int best_task_idx = -1;
      double best_d2 = std::numeric_limits<double>::infinity();
      for (int i = 0; i < static_cast<int>(ego_state.center_positions_.size()); ++i) {
        const double d2 = (ego_state.center_positions_[i] - ed_->region_tour_[1]).squaredNorm();
        if (d2 < best_d2) {
          best_d2 = d2;
          best_task_idx = i;
        }
      }
      if (best_task_idx >= 0 && best_task_idx < static_cast<int>(ego_state.center_hulls_.size()) &&
          !ego_state.center_hulls_[best_task_idx].empty()) {
        next_task_is_hull = true;
      }
    }
    if (next_task_is_hull) {
      ROS_WARN("[Hull->Frontier] No frontier in current hull task, fallback to nearest frontier around task center");
    }

    // No frontier from task-constrained extraction, find the frontier nearest to the current task center.
    Eigen::Vector3d grid_center = ed_->region_tour_[1];

    double min_cost = 100000;
    int min_cost_id = -1;
    bool used_all_cooled_fallback = false;
    for (int i = 0; i < ed_->points_.size(); ++i) {
      if (isPrctTargetCooled(i, ed_->points_[i])) continue;
      vector<Eigen::Vector3d> path;
      double cost = ViewNode::computeCost(
          grid_center, ed_->averages_[i], 0, 0, Eigen::Vector3d(0, 0, 0), 0, path);
      if (ViewNode::isUnreachableCost(cost)) {
        // ROS_ERROR("No frontier in assigned grid, frontier %d unreachable", i);
        continue;
      }
      // ROS_ERROR("No frontier in assigned grid, frontier %d cost: %lf", i, cost);
      if (cost < min_cost) {
        min_cost = cost;
        min_cost_id = i;
      }
    }
    if (min_cost_id < 0) {
      // If every task-constrained candidate is quarantined, avoid empty spinning by
      // falling back to the original C2 nearest-to-assigned-center choice. This is
      // not a new allocation rule; it is only a no-valid-alternative fallback.
      min_cost = 100000;
      for (int i = 0; i < ed_->points_.size(); ++i) {
        vector<Eigen::Vector3d> path;
        double cost = ViewNode::computeCost(
            grid_center, ed_->averages_[i], 0, 0, Eigen::Vector3d(0, 0, 0), 0, path);
        if (ViewNode::isUnreachableCost(cost)) continue;
        if (cost < min_cost) {
          min_cost = cost;
          min_cost_id = i;
        }
      }
      if (min_cost_id >= 0) {
        used_all_cooled_fallback = true;
        std::ostringstream fields;
        fields << std::setprecision(17)
               << "\"fallback_frontier_id\":" << min_cost_id
               << ",\"map_epoch\":" << currentMapVersion()
               << ",\"goal_x\":" << ed_->points_[min_cost_id].x()
               << ",\"goal_y\":" << ed_->points_[min_cost_id].y()
               << ",\"goal_z\":" << ed_->points_[min_cost_id].z();
        logTelemetryEvent("prct_all_cooled_fallback", fields.str());
      }
    }
    if (min_cost_id < 0) {
      int bad_grid = grid_ids.empty() ? -1 : grid_ids.front();
      if (bad_grid >= 0) {
        ROS_WARN("No reachable frontier found from assigned grid center, temporarily drop grid %d",
            bad_grid);

        auto& state = ed_->swarm_state_[ep_->drone_id_ - 1];
        for (auto it = state.grid_ids_.begin(); it != state.grid_ids_.end();) {
          if (*it == bad_grid) {
            it = state.grid_ids_.erase(it);
          } else {
            ++it;
          }
        }
        for (auto it = ed_->last_grid_ids_.begin(); it != ed_->last_grid_ids_.end();) {
          if (*it == bad_grid) {
            it = ed_->last_grid_ids_.erase(it);
          } else {
            ++it;
          }
        }
        ed_->reallocated_ = true;
      } else {
        ROS_WARN("No reachable frontier found from assigned grid center");
      }
      return FAIL;
    }
    next_pos = ed_->points_[min_cost_id];
    next_yaw = ed_->yaws_[min_cost_id];
    selected_frontier_id = min_cost_id;
    target_source = used_all_cooled_fallback
                        ? "all_cooled_original_c2_fallback"
                        : "fallback_nearest_to_assigned_center";
  } else if (frontier_ids.size() == 1) {
    // Single frontier, find the min cost viewpoint for it
    ed_->refined_ids_ = { frontier_ids[0] };
    ed_->unrefined_points_ = { ed_->points_[frontier_ids[0]] };
    ed_->n_points_.clear();
    vector<vector<double>> n_yaws;
    frontier_finder_->getViewpointsInfo(
        pos, { frontier_ids[0] }, ep_->top_view_num_, ep_->max_decay_, ed_->n_points_, n_yaws);
    if (!ed_->n_points_.empty()) {
      telemetry_active_local_view_candidates_ = ed_->n_points_[0];
    }

    // Directly select min cost viewpoint from top N candidates
    double min_cost = 100000;
    int min_cost_id = 0;
    vector<Vector3d> tmp_path;
    for (int i = 0; i < ed_->n_points_[0].size(); ++i) {
      if (edt_environment_->sdf_map_->getOccupancy(ed_->n_points_[0][i]) == SDFMap::OCCUPIED) {
        continue;
      }
      auto tmp_cost = ViewNode::computeCost(
          pos, ed_->n_points_[0][i], yaw[0], n_yaws[0][i], vel, yaw[1], tmp_path);
      if (tmp_cost < min_cost) {
        min_cost = tmp_cost;
        min_cost_id = i;
      }
    }
    next_pos = ed_->n_points_[0][min_cost_id];
    next_yaw = n_yaws[0][min_cost_id];
    selected_frontier_id = frontier_ids[0];
    target_source = "single_frontier_top_view";

    ed_->refined_points_ = { next_pos };
    ed_->refined_views_ = { next_pos + 2.0 * Vector3d(cos(next_yaw), sin(next_yaw), 0) };
  } else {
    // Multiple frontiers, directly use the first frontier's top viewpoint
    next_pos = ed_->points_[frontier_ids[0]];
    next_yaw = ed_->yaws_[frontier_ids[0]];
    selected_frontier_id = frontier_ids[0];
    target_source = "first_frontier_top_view";

    ed_->refined_points_ = { next_pos };
    ed_->refined_views_ = { next_pos + 2.0 * Vector3d(cos(next_yaw), sin(next_yaw), 0) };
  }

  // std::cout << "Next view: " << next_pos.transpose() << ", " << next_yaw << std::endl;
  ed_->next_pos_ = next_pos;
  ed_->next_yaw_ = next_yaw;
  telemetry_active_frontier_id_ = selected_frontier_id;

  // This only records the decision already made above. The expected plan sequence
  // is assigned immediately by planTrajToView and permits failure attribution.
  logTelemetryEvent("target_selection",
      "\"allocation_seq\":" + std::to_string(telemetry_active_allocation_seq_) + "," +
          telemetryTaskFields(next_pos) + ",\"frontier_id\":" +
          std::to_string(telemetry_active_frontier_id_) +
          ",\"expected_plan_seq\":" + std::to_string(telemetry_plan_seq_ + 1) +
          ",\"target_source\":\"" + target_source + "\",\"assigned_grid_first\":" +
          std::to_string(grid_ids.empty() ? -1 : grid_ids.front()) +
           ",\"grid_candidate_count\":" + std::to_string(grid_ids.size()) +
           ",\"frontier_candidate_count\":" + std::to_string(frontier_ids.size()) +
           ",\"local_view_candidate_count\":" +
           std::to_string(telemetry_active_local_view_candidates_.size()) +
           ",\"selected_frontier_id\":" + std::to_string(selected_frontier_id) +
          ",\"goal_x\":" + std::to_string(next_pos.x()) + ",\"goal_y\":" +
          std::to_string(next_pos.y()) + ",\"goal_z\":" + std::to_string(next_pos.z()));

  if (planTrajToView(pos, vel, acc, yaw, next_pos, next_yaw) == FAIL) {
    logTelemetryEvent("explore_result", "\"success\":false,\"reason\":\"trajectory_fail\"");
    return FAIL;
  }

  double total = (ros::Time::now() - t2).toSec();
  ROS_INFO("Total time: %lf", total);
  logTelemetryEvent("explore_result",
      "\"success\":true,\"grid_count\":" + std::to_string(grid_ids.size()) +
          ",\"frontier_count\":" + std::to_string(frontier_ids.size()) +
          ",\"goal_x\":" + std::to_string(next_pos.x()) + ",\"goal_y\":" +
          std::to_string(next_pos.y()) + ",\"goal_z\":" + std::to_string(next_pos.z()) +
          ",\"planning_total_s\":" + std::to_string(total));
  // ROS_ERROR_COND(total > 0.1, "Total time too long!!!");

  return SUCCEED;
}

int C2ExplorationManager::planTrajToView(const Vector3d& pos, const Vector3d& vel,
    const Vector3d& acc, const Vector3d& yaw, const Vector3d& next_pos, const double& next_yaw) {

  // Plan trajectory (position and yaw) to the next viewpoint
  auto t1 = ros::Time::now();
  const double traj_request_wall_s = ros::WallTime::now().toSec();
  const std::uint64_t plan_seq = ++telemetry_plan_seq_;
  logTelemetryEvent("traj_request",
      "\"allocation_seq\":" + std::to_string(telemetry_active_allocation_seq_) + "," +
          telemetryTaskFields(next_pos) + ",\"frontier_id\":" +
          std::to_string(telemetry_active_frontier_id_) + ",\"plan_seq\":" +
          std::to_string(plan_seq) +
          ",\"start_x\":" +
          std::to_string(pos.x()) + ",\"start_y\":" + std::to_string(pos.y()) +
          ",\"start_z\":" + std::to_string(pos.z()) + ",\"goal_x\":" +
          std::to_string(next_pos.x()) + ",\"goal_y\":" + std::to_string(next_pos.y()) +
          ",\"goal_z\":" + std::to_string(next_pos.z()));

  // Compute time lower bound of yaw and use in trajectory generation
  double diff0 = next_yaw - yaw[0];
  double diff1 = fabs(diff0);
  double time_lb = min(diff1, 2 * M_PI - diff1) / ViewNode::yd_;

  // Generate trajectory of x,y,z
  bool goal_unknown = (edt_environment_->sdf_map_->getOccupancy(next_pos) == SDFMap::UNKNOWN);
  // bool start_unknown = (edt_environment_->sdf_map_->getOccupancy(pos) == SDFMap::UNKNOWN);
  bool optimistic = ed_->plan_num_ < ep_->init_plan_num_;
  planner_manager_->path_finder_->reset();
  const int astar_status = planner_manager_->path_finder_->search(pos, next_pos, true);
  if (astar_status != Astar::REACH_END) {
    int occ = edt_environment_->sdf_map_->getOccupancy(next_pos);
    int infl = edt_environment_->sdf_map_->getInflateOccupancy(next_pos);
    bool in_box = edt_environment_->sdf_map_->isInBox(next_pos);
    const auto& astar_diag = planner_manager_->path_finder_->lastSearchDiagnostics();
    ROS_ERROR("No path to next viewpoint");
    ROS_WARN("Path fail detail: start(%.2f %.2f %.2f) goal(%.2f %.2f %.2f) occ=%d infl=%d "
             "in_box=%d",
        pos[0], pos[1], pos[2], next_pos[0], next_pos[1], next_pos[2], occ, infl, in_box ? 1 : 0);
    logTelemetryEvent("traj_result", "\"allocation_seq\":" +
        std::to_string(telemetry_active_allocation_seq_) + "," + telemetryTaskFields(next_pos) +
        ",\"frontier_id\":" + std::to_string(telemetry_active_frontier_id_) +
        ",\"plan_seq\":" + std::to_string(plan_seq) +
        ",\"success\":false,\"reason\":\"astar_fail\",\"duration_wall_s\":" +
        std::to_string(ros::WallTime::now().toSec() - traj_request_wall_s));
    logTelemetryEvent("astar_search_diagnostic",
        "\"plan_seq\":" + std::to_string(plan_seq) + ",\"termination\":\"" +
            Astar::searchTerminationName(astar_diag.termination) + "\",\"elapsed_ros_s\":" +
            std::to_string(astar_diag.elapsed_ros_s) + ",\"elapsed_wall_s\":" +
            std::to_string(astar_diag.elapsed_wall_s) + ",\"expanded_nodes\":" +
            std::to_string(astar_diag.expanded_nodes) + ",\"discovered_nodes\":" +
            std::to_string(astar_diag.discovered_nodes) + ",\"candidate_neighbors\":" +
            std::to_string(astar_diag.candidate_neighbors) + ",\"rejected_out_of_bounds\":" +
            std::to_string(astar_diag.rejected_out_of_bounds) + ",\"rejected_inflated\":" +
            std::to_string(astar_diag.rejected_inflated) + ",\"rejected_unknown\":" +
            std::to_string(astar_diag.rejected_unknown) + ",\"rejected_segment\":" +
            std::to_string(astar_diag.rejected_segment) + ",\"rejected_closed\":" +
            std::to_string(astar_diag.rejected_closed) + ",\"rejected_no_improvement\":" +
            std::to_string(astar_diag.rejected_no_improvement) + ",\"node_pool_used\":" +
            std::to_string(astar_diag.node_pool_used) + ",\"node_pool_capacity\":" +
            std::to_string(astar_diag.node_pool_capacity) + ",\"open_set_size\":" +
            std::to_string(astar_diag.open_set_size) + ",\"start_occupancy\":" +
            std::to_string(astar_diag.start_occupancy) + ",\"end_occupancy\":" +
            std::to_string(astar_diag.end_occupancy) + ",\"start_inflated_occupancy\":" +
            std::to_string(astar_diag.start_inflated_occupancy) + ",\"end_inflated_occupancy\":" +
            std::to_string(astar_diag.end_inflated_occupancy) + ",\"start_in_box\":" +
            std::to_string(astar_diag.start_in_box ? 1 : 0) + ",\"end_in_box\":" +
            std::to_string(astar_diag.end_in_box ? 1 : 0) + ",\"optimistic\":1,\"start_x\":" +
            std::to_string(pos.x()) + ",\"start_y\":" + std::to_string(pos.y()) +
            ",\"start_z\":" + std::to_string(pos.z()) + ",\"goal_x\":" +
            std::to_string(next_pos.x()) + ",\"goal_y\":" + std::to_string(next_pos.y()) +
            ",\"goal_z\":" + std::to_string(next_pos.z()));
    runReachabilityShadowProbe(pos, plan_seq, telemetry_active_frontier_id_);
    registerPrctFailure(telemetry_active_frontier_id_, next_pos);
    runPeerReachabilityShadowProbe(
        pos, next_pos, plan_seq, telemetry_active_frontier_id_, next_yaw);
    return FAIL;
  }
  registerPrctSuccess(telemetry_active_frontier_id_, next_pos);
  ed_->path_next_goal_ = planner_manager_->path_finder_->getPath();
  shortenPath(ed_->path_next_goal_);
  ed_->kino_path_.clear();

  const double radius_far = 7.0;
  const double radius_close = 1.5;
  const double len = Astar::pathLength(ed_->path_next_goal_);
  if (len < radius_close || optimistic) {
    // Next viewpoint is very close, no need to search kinodynamic path, just use waypoints-based
    // optimization
    planner_manager_->planExploreTraj(ed_->path_next_goal_, vel, acc, time_lb);
    ed_->next_goal_ = next_pos;
    // std::cout << "Close goal." << std::endl;
    if (ed_->plan_num_ < ep_->init_plan_num_) {
      ed_->plan_num_++;
      ROS_WARN("init plan.");
    }
  } else if (len > radius_far) {
    // Next viewpoint is far away, select intermediate goal on geometric path (this also deal with
    // dead end)
    // std::cout << "Far goal." << std::endl;
    double len2 = 0.0;
    vector<Eigen::Vector3d> truncated_path = { ed_->path_next_goal_.front() };
    for (int i = 1; i < ed_->path_next_goal_.size() && len2 < radius_far; ++i) {
      auto cur_pt = ed_->path_next_goal_[i];
      len2 += (cur_pt - truncated_path.back()).norm();
      truncated_path.push_back(cur_pt);
    }
    ed_->next_goal_ = truncated_path.back();
    planner_manager_->planExploreTraj(truncated_path, vel, acc, time_lb);
  } else {
    // Search kino path to exactly next viewpoint and optimize
    // std::cout << "Mid goal" << std::endl;
    ed_->next_goal_ = next_pos;

    if (!planner_manager_->kinodynamicReplan(
            pos, vel, acc, ed_->next_goal_, Vector3d(0, 0, 0), time_lb)) {
      logTelemetryEvent("traj_result", "\"allocation_seq\":" +
          std::to_string(telemetry_active_allocation_seq_) + "," + telemetryTaskFields(next_pos) +
          ",\"frontier_id\":" + std::to_string(telemetry_active_frontier_id_) +
          ",\"plan_seq\":" + std::to_string(plan_seq) +
          ",\"success\":false,\"reason\":\"kinodynamic_fail\",\"duration_wall_s\":" +
          std::to_string(ros::WallTime::now().toSec() - traj_request_wall_s));
      return FAIL;
    }
    ed_->kino_path_ = planner_manager_->kino_path_finder_->getKinoTraj(0.02);
  }

  if (planner_manager_->local_data_.position_traj_.getTimeSum() < time_lb - 0.5)
    ROS_ERROR("Lower bound not satified!");

  double traj_plan_time = (ros::Time::now() - t1).toSec();

  t1 = ros::Time::now();
  planner_manager_->planYawExplore(yaw, next_yaw, true, ep_->relax_time_);
  double yaw_time = (ros::Time::now() - t1).toSec();
  ROS_INFO("\033[1m\033[38;5;214mTraj plan time: %lf, yaw plan time: %lf\033[0m", traj_plan_time,
      yaw_time);
  telemetry_active_plan_seq_ = plan_seq;
  logTelemetryEvent("traj_result",
      "\"allocation_seq\":" + std::to_string(telemetry_active_allocation_seq_) + "," +
          telemetryTaskFields(next_pos) + ",\"frontier_id\":" +
          std::to_string(telemetry_active_frontier_id_) + ",\"plan_seq\":" +
          std::to_string(plan_seq) +
          ",\"success\":true,\"traj_plan_s\":" +
          std::to_string(traj_plan_time) + ",\"yaw_plan_s\":" + std::to_string(yaw_time) +
          ",\"path_length_m\":" + std::to_string(len) + ",\"time_lower_bound_s\":" +
          std::to_string(time_lb));

  return SUCCEED;
}

void C2ExplorationManager::runReachabilityShadowProbe(
    const Vector3d& start_pos, const std::uint64_t plan_seq, const int failed_frontier_id) {
  if (reachability_shadow_max_candidates_ <= 0 || !planner_manager_ ||
      !planner_manager_->path_finder_) {
    return;
  }

  int probed = 0;
  for (const int candidate_id : telemetry_active_frontier_candidates_) {
    if (probed >= reachability_shadow_max_candidates_) break;
    if (candidate_id < 0 || candidate_id == failed_frontier_id ||
        candidate_id >= static_cast<int>(ed_->points_.size())) {
      continue;
    }

    const Vector3d candidate = ed_->points_[candidate_id];
    planner_manager_->path_finder_->reset();
    const double start_wall_s = ros::WallTime::now().toSec();
    const int status = planner_manager_->path_finder_->search(start_pos, candidate, true);
    const double duration_wall_s = ros::WallTime::now().toSec() - start_wall_s;
    const auto& diagnostics = planner_manager_->path_finder_->lastSearchDiagnostics();
    const bool success = status == Astar::REACH_END;
    double path_length = 0.0;
    if (success) path_length = Astar::pathLength(planner_manager_->path_finder_->getPath());

    std::ostringstream fields;
    fields << std::setprecision(17)
           << "\"plan_seq\":" << plan_seq
           << ",\"failed_frontier_id\":" << failed_frontier_id
           << ",\"candidate_frontier_id\":" << candidate_id
           << ",\"candidate_source\":\"frontier_top_view\""
           << ",\"candidate_local_view_index\":-1"
           << ",\"frontier_candidate_count\":"
           << telemetry_active_frontier_candidates_.size()
           << ",\"local_view_candidate_count\":"
           << telemetry_active_local_view_candidates_.size()
           << ",\"success\":" << (success ? "true" : "false")
           << ",\"termination\":\""
           << Astar::searchTerminationName(diagnostics.termination) << "\""
           << ",\"duration_wall_s\":" << duration_wall_s
           << ",\"path_length_m\":" << path_length
           << ",\"candidate_x\":" << candidate.x()
           << ",\"candidate_y\":" << candidate.y()
           << ",\"candidate_z\":" << candidate.z();
    logTelemetryEvent("reachability_shadow_probe", fields.str());
    ++probed;
  }

  // A single selected frontier can still expose several top viewpoints. C2
  // ranks them geometrically before A*; this diagnostic checks the other
  // same-frontier viewpoints without changing the selected target.
  for (size_t local_index = 0;
       local_index < telemetry_active_local_view_candidates_.size() &&
       probed < reachability_shadow_max_candidates_;
       ++local_index) {
    const Vector3d& candidate = telemetry_active_local_view_candidates_[local_index];
    if ((candidate - ed_->next_pos_).squaredNorm() <= 1e-8) {
      continue;
    }

    planner_manager_->path_finder_->reset();
    const double start_wall_s = ros::WallTime::now().toSec();
    const int status = planner_manager_->path_finder_->search(start_pos, candidate, true);
    const double duration_wall_s = ros::WallTime::now().toSec() - start_wall_s;
    const auto& diagnostics = planner_manager_->path_finder_->lastSearchDiagnostics();
    const bool success = status == Astar::REACH_END;
    double path_length = 0.0;
    if (success) path_length = Astar::pathLength(planner_manager_->path_finder_->getPath());

    std::ostringstream fields;
    fields << std::setprecision(17)
           << "\"plan_seq\":" << plan_seq
           << ",\"failed_frontier_id\":" << failed_frontier_id
           << ",\"candidate_frontier_id\":" << failed_frontier_id
           << ",\"candidate_source\":\"local_top_view\""
           << ",\"candidate_local_view_index\":" << local_index
           << ",\"frontier_candidate_count\":"
           << telemetry_active_frontier_candidates_.size()
           << ",\"local_view_candidate_count\":"
           << telemetry_active_local_view_candidates_.size()
           << ",\"success\":" << (success ? "true" : "false")
           << ",\"termination\":\""
           << Astar::searchTerminationName(diagnostics.termination) << "\""
           << ",\"duration_wall_s\":" << duration_wall_s
           << ",\"path_length_m\":" << path_length
           << ",\"candidate_x\":" << candidate.x()
           << ",\"candidate_y\":" << candidate.y()
           << ",\"candidate_z\":" << candidate.z();
    logTelemetryEvent("reachability_shadow_probe", fields.str());
    ++probed;
  }
}

void C2ExplorationManager::runPeerReachabilityShadowProbe(
    const Vector3d& failed_start, const Vector3d& failed_goal,
    const std::uint64_t plan_seq, const int failed_frontier_id,
    const double failed_goal_yaw) {
  if (reachability_peer_shadow_max_peers_ <= 0 || !planner_manager_ ||
      !planner_manager_->path_finder_ || !ed_ || !ep_) {
    return;
  }

  const int self_id = ep_->drone_id_;
  int probed = 0;
  for (int peer_id = 1; peer_id <= static_cast<int>(ed_->swarm_state_.size()); ++peer_id) {
    if (peer_id == self_id || probed >= reachability_peer_shadow_max_peers_) continue;
    const auto& peer = ed_->swarm_state_[peer_id - 1];
    if (peer.pos_.squaredNorm() < 1e-12 || peer.stamp_ <= 0.0) continue;

    planner_manager_->path_finder_->reset();
    const double start_wall_s = ros::WallTime::now().toSec();
    const int status = planner_manager_->path_finder_->search(peer.pos_, failed_goal, true);
    const double duration_wall_s = ros::WallTime::now().toSec() - start_wall_s;
    const auto& diagnostics = planner_manager_->path_finder_->lastSearchDiagnostics();
    const bool success = status == Astar::REACH_END;
    const double path_length = success
        ? Astar::pathLength(planner_manager_->path_finder_->getPath()) : 0.0;

    std::ostringstream fields;
    fields << std::setprecision(17)
           << "\"plan_seq\":" << plan_seq
           << ",\"failed_frontier_id\":" << failed_frontier_id
           << ",\"peer_drone_id\":" << peer_id
           << ",\"peer_state_stamp_s\":" << peer.stamp_
           << ",\"peer_state_age_s\":"
           << std::max(0.0, ros::Time::now().toSec() - peer.stamp_)
           << ",\"success\":" << (success ? "true" : "false")
           << ",\"termination\":\""
           << Astar::searchTerminationName(diagnostics.termination) << "\""
           << ",\"duration_wall_s\":" << duration_wall_s
           << ",\"path_length_m\":" << path_length
           << ",\"failed_start_x\":" << failed_start.x()
           << ",\"failed_start_y\":" << failed_start.y()
           << ",\"failed_start_z\":" << failed_start.z()
           << ",\"peer_start_x\":" << peer.pos_.x()
           << ",\"peer_start_y\":" << peer.pos_.y()
           << ",\"peer_start_z\":" << peer.pos_.z()
           << ",\"goal_x\":" << failed_goal.x()
           << ",\"goal_y\":" << failed_goal.y()
           << ",\"goal_z\":" << failed_goal.z();
    logTelemetryEvent("peer_reachability_shadow_probe", fields.str());
    ++probed;
  }

  runPeerLocalMapReachabilityShadowProbe(
      failed_goal, plan_seq, failed_frontier_id, failed_goal_yaw);
}

void C2ExplorationManager::runPeerLocalMapReachabilityShadowProbe(
    const Vector3d& failed_goal, const std::uint64_t plan_seq,
    const int failed_frontier_id, const double failed_goal_yaw) {
  if (reachability_peer_shadow_max_peers_ <= 0 || !ed_ || !ep_ ||
      !peer_reachability_query_pub_) {
    return;
  }

  const int self_id = ep_->drone_id_;
  const double now_wall_s = ros::WallTime::now().toSec();

  // B2 shadow mode: query peers and record certificates, but never enter PRCT.
  if (!prct_enable_peer_takeover_) {
    int queried = 0;
    for (int peer_id = 1;
         peer_id <= static_cast<int>(ed_->swarm_state_.size()) &&
         queried < reachability_peer_shadow_max_peers_;
         ++peer_id) {
      if (peer_id == self_id) continue;
      const auto& peer = ed_->swarm_state_[peer_id - 1];
      if (peer.pos_.squaredNorm() < 1e-12 || peer.stamp_ <= 0.0) continue;

      ++peer_reachability_request_seq_;
      exploration_manager::PeerReachabilityQuery msg;
      msg.request_id = peer_reachability_request_seq_;
      msg.owner_drone_id = static_cast<uint8_t>(self_id);
      msg.target_drone_id = static_cast<uint8_t>(peer_id);
      msg.owner_plan_seq = plan_seq;
      msg.failed_frontier_id = failed_frontier_id;
      msg.goal_x = failed_goal.x();
      msg.goal_y = failed_goal.y();
      msg.goal_z = failed_goal.z();
      msg.goal_yaw = failed_goal_yaw;
      peer_reachability_query_pub_.publish(msg);

      std::ostringstream fields;
      fields << std::setprecision(17)
             << "\"request_id\":" << msg.request_id
             << ",\"target_drone_id\":" << static_cast<int>(msg.target_drone_id)
             << ",\"owner_plan_seq\":" << msg.owner_plan_seq
             << ",\"failed_frontier_id\":" << msg.failed_frontier_id
             << ",\"goal_x\":" << failed_goal.x()
             << ",\"goal_y\":" << failed_goal.y()
             << ",\"goal_z\":" << failed_goal.z();
      logTelemetryEvent("peer_local_map_reachability_query", fields.str());
      ++queried;
    }
    return;
  }

  if (isPrctTakeoverCooled(failed_frontier_id, failed_goal)) {
    std::ostringstream fields;
    fields << std::setprecision(17)
           << "\"frontier_id\":" << failed_frontier_id
           << ",\"owner_plan_seq\":" << plan_seq
           << ",\"goal_x\":" << failed_goal.x()
           << ",\"goal_y\":" << failed_goal.y()
           << ",\"goal_z\":" << failed_goal.z();
    logTelemetryEvent("prct_takeover_suppressed", fields.str());
    return;
  }

  if (prct_enable_peer_takeover_ && c3_enable_marginal_gate_ &&
      prctTakeoverAttemptCount(failed_frontier_id, failed_goal) >=
          c3_max_takeover_attempts_) {
    std::ostringstream fields;
    fields << std::setprecision(17)
           << "\"frontier_id\":" << failed_frontier_id
           << ",\"owner_plan_seq\":" << plan_seq
           << ",\"goal_x\":" << failed_goal.x()
           << ",\"goal_y\":" << failed_goal.y()
           << ",\"goal_z\":" << failed_goal.z()
           << ",\"attempt_count\":" << prctTakeoverAttemptCount(
               failed_frontier_id, failed_goal)
           << ",\"max_attempts\":" << c3_max_takeover_attempts_;
    logTelemetryEvent("c3_takeover_exhausted", fields.str());
    return;
  }

  if (prct_enable_peer_takeover_ && c3_enable_marginal_gate_) {
    const int repeat_count = prctFailureRepeatCount(failed_frontier_id, failed_goal);
    if (repeat_count < c3_min_repeat_count_) {
      std::ostringstream fields;
      fields << std::setprecision(17)
             << "\"frontier_id\":" << failed_frontier_id
             << ",\"owner_plan_seq\":" << plan_seq
             << ",\"goal_x\":" << failed_goal.x()
             << ",\"goal_y\":" << failed_goal.y()
             << ",\"goal_z\":" << failed_goal.z()
             << ",\"repeat_count\":" << repeat_count
             << ",\"min_repeat_count\":" << c3_min_repeat_count_;
      logTelemetryEvent("c3_handoff_skipped_low_repeat", fields.str());
      return;
    }
  }

  if (peer_handoff_pending_) {
    const bool same_goal = (peer_handoff_goal_ - failed_goal).norm() < 0.05;
    const bool window_active =
        now_wall_s - peer_handoff_started_wall_s_ < 0.5;
    if (same_goal && window_active) {
      return;
    }
    clearPeerHandoff();
  }
  if (peer_handoff_published_) {
    clearPeerHandoff();
  }

  peer_handoff_pending_ = true;
  peer_handoff_plan_seq_ = plan_seq;
  peer_handoff_frontier_id_ = failed_frontier_id;
  peer_handoff_goal_ = failed_goal;
  peer_handoff_goal_yaw_ = failed_goal_yaw;
  peer_handoff_started_wall_s_ = now_wall_s;
  peer_handoff_cert_window_end_wall_s_ =
      now_wall_s + prct_peer_cert_wait_s_ + c3_peer_cert_grace_s_;
  peer_handoff_deadline_wall_s_ = now_wall_s + prct_peer_handoff_timeout_s_;
  if (peer_handoff_cert_window_end_wall_s_ > peer_handoff_deadline_wall_s_) {
    peer_handoff_cert_window_end_wall_s_ = peer_handoff_deadline_wall_s_;
  }
  peer_handoff_map_version_ = currentMapVersion();
  peer_handoff_published_ = false;
  peer_handoff_observed_ = false;
  peer_handoff_best_peer_id_ = -1;
  pending_peer_certificates_.clear();
  int queried = 0;
  for (int peer_id = 1;
       peer_id <= static_cast<int>(ed_->swarm_state_.size()) &&
       queried < reachability_peer_shadow_max_peers_;
       ++peer_id) {
    if (peer_id == self_id) continue;
    const auto& peer = ed_->swarm_state_[peer_id - 1];
    if (peer.pos_.squaredNorm() < 1e-12 || peer.stamp_ <= 0.0) continue;

    ++peer_reachability_request_seq_;
    exploration_manager::PeerReachabilityQuery msg;
    msg.request_id = peer_reachability_request_seq_;
    msg.owner_drone_id = static_cast<uint8_t>(self_id);
    msg.target_drone_id = static_cast<uint8_t>(peer_id);
    msg.owner_plan_seq = plan_seq;
    msg.failed_frontier_id = failed_frontier_id;
    msg.goal_x = failed_goal.x();
    msg.goal_y = failed_goal.y();
    msg.goal_z = failed_goal.z();
    msg.goal_yaw = failed_goal_yaw;
    peer_reachability_query_pub_.publish(msg);

    std::ostringstream fields;
    fields << std::setprecision(17)
           << "\"request_id\":" << msg.request_id
           << ",\"target_drone_id\":" << static_cast<int>(msg.target_drone_id)
           << ",\"owner_plan_seq\":" << msg.owner_plan_seq
           << ",\"failed_frontier_id\":" << msg.failed_frontier_id
           << ",\"goal_x\":" << failed_goal.x()
           << ",\"goal_y\":" << failed_goal.y()
           << ",\"goal_z\":" << failed_goal.z();
    logTelemetryEvent("peer_local_map_reachability_query", fields.str());
    ++queried;
  }
}

void C2ExplorationManager::peerReachabilityQueryCallback(
    const exploration_manager::PeerReachabilityQueryConstPtr& msg) {
  if (!msg || !ep_ || !ed_ || !planner_manager_ ||
      !planner_manager_->path_finder_ ||
      msg->target_drone_id != static_cast<uint8_t>(ep_->drone_id_)) {
    return;
  }

  const auto& self = ed_->swarm_state_[ep_->drone_id_ - 1];
  if (self.pos_.squaredNorm() < 1e-12 || self.stamp_ <= 0.0) return;

  const Vector3d goal(msg->goal_x, msg->goal_y, msg->goal_z);
  planner_manager_->path_finder_->reset();
  const double start_wall_s = ros::WallTime::now().toSec();
  const int status = planner_manager_->path_finder_->search(self.pos_, goal, true);
  const double duration_wall_s = ros::WallTime::now().toSec() - start_wall_s;
  const auto& diagnostics = planner_manager_->path_finder_->lastSearchDiagnostics();
  const bool success = status == Astar::REACH_END;
  const double path_length = success
      ? Astar::pathLength(planner_manager_->path_finder_->getPath()) : 0.0;

  exploration_manager::PeerReachabilityResult result;
  result.request_id = msg->request_id;
  result.owner_drone_id = msg->owner_drone_id;
  result.peer_drone_id = static_cast<uint8_t>(ep_->drone_id_);
  result.owner_plan_seq = msg->owner_plan_seq;
  result.failed_frontier_id = msg->failed_frontier_id;
  result.success = success;
  result.termination_name = Astar::searchTerminationName(diagnostics.termination);
  result.duration_wall_s = duration_wall_s;
  result.path_length_m = path_length;
  result.peer_start_x = self.pos_.x();
  result.peer_start_y = self.pos_.y();
  result.peer_start_z = self.pos_.z();
  result.peer_state_age_s =
      std::max(0.0, ros::Time::now().toSec() - self.stamp_);
  result.goal_yaw = msg->goal_yaw;
  peer_reachability_result_pub_.publish(result);

  std::ostringstream fields;
  fields << std::setprecision(17)
         << "\"request_id\":" << result.request_id
         << ",\"peer_drone_id\":" << static_cast<int>(result.peer_drone_id)
         << ",\"owner_plan_seq\":" << result.owner_plan_seq
         << ",\"failed_frontier_id\":" << result.failed_frontier_id
         << ",\"success\":" << (success ? "true" : "false")
         << ",\"termination\":\"" << result.termination_name << "\""
         << ",\"duration_wall_s\":" << duration_wall_s
         << ",\"path_length_m\":" << path_length
         << ",\"peer_start_x\":" << self.pos_.x()
         << ",\"peer_start_y\":" << self.pos_.y()
         << ",\"peer_start_z\":" << self.pos_.z()
         << ",\"peer_state_age_s\":" << result.peer_state_age_s;
  logTelemetryEvent("peer_local_map_reachability_response", fields.str());
}

void C2ExplorationManager::peerReachabilityResultCallback(
    const exploration_manager::PeerReachabilityResultConstPtr& msg) {
  if (!msg || !ep_ || !ed_ ||
      msg->owner_drone_id != static_cast<uint8_t>(ep_->drone_id_)) {
    return;
  }

  const double now_wall_s = ros::WallTime::now().toSec();
  double peer_load = 0.0;
  const int peer_drone_id = static_cast<int>(msg->peer_drone_id);
  if (peer_drone_id >= 1 &&
      peer_drone_id <= static_cast<int>(ed_->swarm_state_.size())) {
    peer_load = static_cast<double>(
        ed_->swarm_state_[peer_drone_id - 1].center_positions_.size());
  }

  std::ostringstream fields;
  fields << std::setprecision(17)
         << "\"request_id\":" << msg->request_id
         << ",\"peer_drone_id\":" << peer_drone_id
         << ",\"owner_plan_seq\":" << msg->owner_plan_seq
         << ",\"failed_frontier_id\":" << msg->failed_frontier_id
         << ",\"success\":" << (msg->success ? "true" : "false")
         << ",\"termination\":\"" << msg->termination_name << "\""
         << ",\"duration_wall_s\":" << msg->duration_wall_s
         << ",\"path_length_m\":" << msg->path_length_m
         << ",\"peer_start_x\":" << msg->peer_start_x
         << ",\"peer_start_y\":" << msg->peer_start_y
         << ",\"peer_start_z\":" << msg->peer_start_z
         << ",\"peer_state_age_s\":" << msg->peer_state_age_s
         << ",\"peer_load\":" << peer_load;
  logTelemetryEvent("peer_local_map_reachability_probe", fields.str());

  if (!prct_enable_peer_takeover_) {
    return;
  }
  if (peer_handoff_pending_ && !peer_handoff_published_ &&
      now_wall_s <= peer_handoff_cert_window_end_wall_s_ + 1e-6 &&
      msg->owner_plan_seq == peer_handoff_plan_seq_ && msg->success &&
      msg->peer_state_age_s <= prct_peer_state_max_age_s_) {
    PendingPeerCertificate cert;
    cert.peer_id = peer_drone_id;
    cert.request_id = msg->request_id;
    cert.path_length_m = msg->path_length_m;
    cert.peer_state_age_s = msg->peer_state_age_s;
    cert.duration_wall_s = msg->duration_wall_s;
    cert.peer_load = peer_load;
    cert.peer_trust = peerTrust(cert.peer_id);
    cert.peer_marginal_cost_s = computePeerMarginalCostS(cert);
    cert.expected_benefit_s =
        estimateOwnerStuckCostS() - cert.peer_marginal_cost_s;
    bool replaced = false;
    for (auto& existing : pending_peer_certificates_) {
      if (existing.peer_id == cert.peer_id) {
        existing = cert;
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      pending_peer_certificates_.push_back(cert);
    }
  }
}

void C2ExplorationManager::publishPeerTakeoverGoal(
    const int peer_id, const std::uint64_t plan_seq, const int frontier_id,
    const Vector3d& goal, const double goal_yaw) {
  if (!peer_takeover_goal_pub_ || !ep_ || peer_id <= 0) return;
  if (isPrctTakeoverCooled(frontier_id, goal)) {
    peer_handoff_fallback_reason_ = "takeover_cooled";
    std::ostringstream fields;
    fields << std::setprecision(17)
           << "\"reason\":\"takeover_cooled\""
           << ",\"frontier_id\":" << frontier_id
           << ",\"peer_id\":" << peer_id;
    logTelemetryEvent("peer_handoff_fallback", fields.str());
    clearPeerHandoff();
    return;
  }

  ++peer_takeover_request_seq_;
  peer_handoff_request_id_ = peer_takeover_request_seq_;
  exploration_manager::PeerTakeoverGoal msg;
  msg.request_id = peer_handoff_request_id_;
  msg.owner_drone_id = static_cast<uint8_t>(ep_->drone_id_);
  msg.target_drone_id = static_cast<uint8_t>(peer_id);
  msg.owner_plan_seq = plan_seq;
  msg.failed_frontier_id = frontier_id;
  msg.goal_x = goal.x();
  msg.goal_y = goal.y();
  msg.goal_z = goal.z();
  msg.goal_yaw = goal_yaw;
  msg.issued_wall_s = ros::WallTime::now().toSec();
  peer_takeover_goal_pub_.publish(msg);
  peer_handoff_published_ = true;
  peer_handoff_observed_ = false;
  peer_handoff_deadline_wall_s_ =
      ros::WallTime::now().toSec() + prct_peer_handoff_timeout_s_;
  ++peer_handoff_publish_count_;
  registerPrctTakeover(frontier_id, goal, c3_enable_marginal_gate_);

  std::ostringstream fields;
  fields << std::setprecision(17)
         << "\"request_id\":" << peer_handoff_request_id_
         << ",\"target_drone_id\":" << peer_id
         << ",\"owner_plan_seq\":" << plan_seq
         << ",\"failed_frontier_id\":" << frontier_id
         << ",\"goal_x\":" << goal.x()
         << ",\"goal_y\":" << goal.y()
         << ",\"goal_z\":" << goal.z()
         << ",\"goal_yaw\":" << goal_yaw
         << ",\"publish_count\":" << peer_handoff_publish_count_;
  logTelemetryEvent("peer_takeover_goal_sent", fields.str());
}

int C2ExplorationManager::selectBestPeerCertificate() const {
  if (pending_peer_certificates_.empty()) return -1;
  int best_id = -1;
  double best_path = std::numeric_limits<double>::infinity();
  double best_age = std::numeric_limits<double>::infinity();
  double best_load = std::numeric_limits<double>::infinity();
  double best_benefit = -std::numeric_limits<double>::infinity();
  for (const auto& cert : pending_peer_certificates_) {
    if (cert.peer_id <= 0) continue;
    if (c3_enable_marginal_gate_) {
      if (cert.peer_trust + 1e-9 < c3_trust_threshold_) continue;
      if (cert.expected_benefit_s <= c3_benefit_margin_s_ + 1e-9) continue;
      const bool better =
          best_id < 0 ||
          cert.expected_benefit_s > best_benefit + 1e-9 ||
          (std::abs(cert.expected_benefit_s - best_benefit) <= 1e-9 &&
           (cert.path_length_m < best_path - 0.05 ||
            (std::abs(cert.path_length_m - best_path) <= 0.05 &&
             cert.peer_state_age_s < best_age - 1e-6) ||
            (std::abs(cert.path_length_m - best_path) <= 0.05 &&
             std::abs(cert.peer_state_age_s - best_age) <= 1e-6 &&
             cert.peer_load < best_load - 1e-6)));
      if (better) {
        best_id = cert.peer_id;
        best_path = cert.path_length_m;
        best_age = cert.peer_state_age_s;
        best_load = cert.peer_load;
        best_benefit = cert.expected_benefit_s;
      }
      continue;
    }
    const double path_delta = std::abs(cert.path_length_m - best_path);
    const double age_delta = std::abs(cert.peer_state_age_s - best_age);
    const bool better =
        best_id < 0 ||
        cert.path_length_m < best_path - 0.05 ||
        (path_delta <= 0.05 && cert.peer_state_age_s < best_age - 1e-6) ||
        (path_delta <= 0.05 && age_delta <= 1e-6 &&
         cert.peer_load < best_load - 1e-6);
    if (better) {
      best_id = cert.peer_id;
      best_path = cert.path_length_m;
      best_age = cert.peer_state_age_s;
      best_load = cert.peer_load;
    }
  }
  return best_id;
}

double C2ExplorationManager::estimateOwnerStuckCostS() const {
  const double now_wall_s = ros::WallTime::now().toSec();
  const std::string key = prctCooldownKey(
      peer_handoff_frontier_id_, peer_handoff_goal_, currentMapVersion());
  double elapsed = 0.0;
  const auto chain_it = c3_failure_chain_start_wall_s_.find(key);
  if (chain_it != c3_failure_chain_start_wall_s_.end()) {
    elapsed = std::max(0.0, now_wall_s - chain_it->second);
  } else if (peer_handoff_started_wall_s_ > 0.0) {
    elapsed = std::max(0.0, now_wall_s - peer_handoff_started_wall_s_);
  }
  const int repeat_count = prctFailureRepeatCount(
      peer_handoff_frontier_id_, peer_handoff_goal_);
  const double repeat_cost = c3_owner_repeat_cost_s_ *
      std::max(0, repeat_count - 1);
  return c3_owner_stuck_alpha_ * elapsed +
      c3_owner_fallback_penalty_s_ + repeat_cost;
}

double C2ExplorationManager::computePeerMarginalCostS(
    const PendingPeerCertificate& cert) const {
  const double travel =
      c3_nominal_speed_m_s_ > 1e-6
          ? cert.path_length_m / c3_nominal_speed_m_s_ : cert.path_length_m;
  const double trust = std::min(1.0, std::max(0.0, cert.peer_trust));
  const double trust_penalty = (1.0 - trust) * c3_trust_penalty_s_;
  return travel + c3_load_weight_ * cert.peer_load +
         c3_handoff_overhead_s_ + trust_penalty;
}

double C2ExplorationManager::peerTrust(const int peer_id) const {
  const auto it = peer_trust_.find(peer_id);
  if (it == peer_trust_.end()) return 0.5;
  return it->second.trust;
}

void C2ExplorationManager::updatePeerTrust(
    const int peer_id, const std::uint8_t status) {
  if (peer_id <= 0) return;
  PeerTrustEntry& entry = peer_trust_[peer_id];
  if (status == TAKEOVER_COMPLETED) {
    ++entry.success_count;
    entry.alpha += 1.0;
  } else if (status == TAKEOVER_ABORTED) {
    ++entry.abort_count;
    entry.beta += 1.0;
  } else if (status == TAKEOVER_REJECTED) {
    ++entry.reject_count;
    entry.beta += 1.0;
  } else if (status == TAKEOVER_STALE) {
    ++entry.stale_count;
    entry.beta += 1.0;
  }
  entry.trust = entry.alpha / (entry.alpha + entry.beta);
  std::ostringstream fields;
  fields << "\"peer_id\":" << peer_id
         << ",\"status\":" << static_cast<int>(status)
         << ",\"trust\":" << entry.trust
         << ",\"success_count\":" << entry.success_count
         << ",\"reject_count\":" << entry.reject_count
         << ",\"abort_count\":" << entry.abort_count
         << ",\"stale_count\":" << entry.stale_count;
  logTelemetryEvent("c3_peer_trust_update", fields.str());
}

bool C2ExplorationManager::isPrctTakeoverCooled(
    const int frontier_id, const Vector3d& goal) const {
  if (!prct_enable_peer_takeover_ || frontier_id < 0) return false;
  if (isC3TakeoverCompleted(frontier_id)) return true;
  const auto it = prct_takeover_cooldowns_.find(
      prctCooldownKey(frontier_id, goal, currentMapVersion()));
  if (it == prct_takeover_cooldowns_.end()) return false;
  return ros::WallTime::now().toSec() < it->second.cooldown_until_wall_s;
}

bool C2ExplorationManager::isC3TakeoverCompleted(const int frontier_id) const {
  if (!prct_enable_peer_takeover_ || frontier_id < 0) return false;
  const auto it = c3_takeover_completed_until_wall_s_.find(frontier_id);
  if (it == c3_takeover_completed_until_wall_s_.end()) return false;
  return ros::WallTime::now().toSec() < it->second;
}

int C2ExplorationManager::prctTakeoverAttemptCount(
    const int frontier_id, const Vector3d& goal) const {
  if (!prct_enable_peer_takeover_ || frontier_id < 0) return 0;
  const auto it = prct_takeover_cooldowns_.find(
      prctCooldownKey(frontier_id, goal, currentMapVersion()));
  return it == prct_takeover_cooldowns_.end() ? 0 : it->second.repeat_count;
}

void C2ExplorationManager::registerPrctTakeover(
    const int frontier_id, const Vector3d& goal,
    const bool long_cooldown) {
  if (!prct_enable_peer_takeover_ || frontier_id < 0) return;
  const uint64_t map_version = currentMapVersion();
  auto& entry = prct_takeover_cooldowns_[
      prctCooldownKey(frontier_id, goal, map_version)];
  ++entry.repeat_count;
  entry.map_version = map_version;
  double cooldown_s = prct_cooldown_s_;
  if (long_cooldown && c3_enable_marginal_gate_) {
    cooldown_s = c3_takeover_cooldown_s_;
  }
  entry.cooldown_until_wall_s =
      ros::WallTime::now().toSec() + cooldown_s;
}

int C2ExplorationManager::prctFailureRepeatCount(
    const int frontier_id, const Vector3d& goal) const {
  if (frontier_id < 0) return 0;
  const uint64_t map_version = currentMapVersion();
  const std::string key = prctCooldownKey(frontier_id, goal, map_version);
  if (c3MarginalGateActive()) {
    const auto it = c3_failure_repeat_counts_.find(key);
    return it == c3_failure_repeat_counts_.end() ? 0 : it->second;
  }
  const auto it = prct_cooldowns_.find(
      key);
  return it == prct_cooldowns_.end() ? 0 : it->second.repeat_count;
}

void C2ExplorationManager::peerTakeoverGoalCallback(
    const exploration_manager::PeerTakeoverGoalConstPtr& msg) {
  if (!msg || !ep_ || !ed_ ||
      msg->target_drone_id != static_cast<uint8_t>(ep_->drone_id_)) {
    return;
  }
  const double now_wall_s = ros::WallTime::now().toSec();
  auto it = pending_peer_takeover_goals_.begin();
  while (it != pending_peer_takeover_goals_.end()) {
    if (now_wall_s - it->received_wall_s > peer_takeover_goal_timeout_s_) {
      it = pending_peer_takeover_goals_.erase(it);
    } else {
      ++it;
    }
  }

  const double issued_age_s = now_wall_s - msg->issued_wall_s;
  if (issued_age_s < -0.1 || issued_age_s > peer_takeover_goal_timeout_s_) {
    sendPeerTakeoverReceipt(msg->owner_drone_id, msg->request_id,
        msg->owner_plan_seq, msg->failed_frontier_id,
        Vector3d(msg->goal_x, msg->goal_y, msg->goal_z), msg->goal_yaw,
        msg->issued_wall_s, TAKEOVER_STALE, "STALE", "stale_issued");
    logTelemetryEvent("peer_takeover_goal_stale",
        "\"request_id\":" + std::to_string(msg->request_id) +
            ",\"owner_plan_seq\":" + std::to_string(msg->owner_plan_seq) +
            ",\"issued_age_s\":" + std::to_string(issued_age_s));
    return;
  }

  for (const auto& existing : pending_peer_takeover_goals_) {
    if (existing.request_id == msg->request_id &&
        existing.owner_drone_id == msg->owner_drone_id) {
      sendPeerTakeoverReceipt(msg->owner_drone_id, msg->request_id,
          msg->owner_plan_seq, msg->failed_frontier_id,
          Vector3d(msg->goal_x, msg->goal_y, msg->goal_z), msg->goal_yaw,
          msg->issued_wall_s, TAKEOVER_STALE, "STALE", "duplicate_request");
      logTelemetryEvent("peer_takeover_goal_duplicate",
          "\"request_id\":" + std::to_string(msg->request_id) +
              ",\"owner_plan_seq\":" + std::to_string(msg->owner_plan_seq));
      return;
    }
  }

  if (static_cast<int>(pending_peer_takeover_goals_.size()) >=
      peer_takeover_goal_max_queue_) {
    sendPeerTakeoverReceipt(msg->owner_drone_id, msg->request_id,
        msg->owner_plan_seq, msg->failed_frontier_id,
        Vector3d(msg->goal_x, msg->goal_y, msg->goal_z), msg->goal_yaw,
        msg->issued_wall_s, TAKEOVER_REJECTED, "REJECTED", "queue_full");
    logTelemetryEvent("peer_takeover_goal_dropped",
        "\"reason\":\"queue_full\",\"owner_plan_seq\":" +
            std::to_string(msg->owner_plan_seq));
    return;
  }

  PendingPeerTakeoverGoal goal;
  goal.owner_plan_seq = msg->owner_plan_seq;
  goal.owner_drone_id = msg->owner_drone_id;
  goal.request_id = msg->request_id;
  goal.issued_wall_s = msg->issued_wall_s;
  goal.frontier_id = msg->failed_frontier_id;
  goal.goal = Vector3d(msg->goal_x, msg->goal_y, msg->goal_z);
  goal.goal_yaw = msg->goal_yaw;
  goal.received_wall_s = now_wall_s;
  pending_peer_takeover_goals_.push_back(goal);
  sendPeerTakeoverReceipt(msg->owner_drone_id, msg->request_id,
      msg->owner_plan_seq, msg->failed_frontier_id, goal.goal, goal.goal_yaw,
      msg->issued_wall_s, TAKEOVER_ACCEPTED, "ACCEPTED", "queued");
  logTelemetryEvent("peer_takeover_goal_received",
      "\"owner_plan_seq\":" + std::to_string(msg->owner_plan_seq) +
          ",\"frontier_id\":" + std::to_string(msg->failed_frontier_id) +
          ",\"goal_x\":" + std::to_string(msg->goal_x) +
          ",\"goal_y\":" + std::to_string(msg->goal_y) +
          ",\"goal_z\":" + std::to_string(msg->goal_z) +
          ",\"goal_yaw\":" + std::to_string(msg->goal_yaw));
}

bool C2ExplorationManager::takeNextPeerTakeoverGoal(
    Vector3d& goal, double& yaw, int& frontier_id,
    std::uint64_t& owner_plan_seq, std::uint8_t& owner_drone_id,
    std::uint64_t& request_id, double& issued_wall_s) {
  const double now_wall_s = ros::WallTime::now().toSec();
  while (!pending_peer_takeover_goals_.empty()) {
    auto it = pending_peer_takeover_goals_.begin();
    if (now_wall_s - it->received_wall_s > peer_takeover_goal_timeout_s_) {
      pending_peer_takeover_goals_.erase(it);
      continue;
    }
    goal = it->goal;
    yaw = it->goal_yaw;
    frontier_id = it->frontier_id;
    owner_plan_seq = it->owner_plan_seq;
    owner_drone_id = it->owner_drone_id;
    request_id = it->request_id;
    issued_wall_s = it->issued_wall_s;
    pending_peer_takeover_goals_.erase(it);
    return true;
  }
  return false;
}

void C2ExplorationManager::peerTakeoverReceiptCallback(
    const exploration_manager::PeerTakeoverReceiptConstPtr& msg) {
  if (!msg || !ep_) return;
  std::ostringstream fields;
  fields << std::setprecision(17)
         << "\"request_id\":" << msg->request_id
         << ",\"target_drone_id\":" << static_cast<int>(msg->target_drone_id)
         << ",\"owner_plan_seq\":" << msg->owner_plan_seq
         << ",\"failed_frontier_id\":" << msg->failed_frontier_id
         << ",\"status\":\"" << msg->status_name << "\""
         << ",\"reason\":\"" << msg->reason << "\"";
  logTelemetryEvent("peer_takeover_receipt", fields.str());

  if (msg->owner_drone_id != static_cast<uint8_t>(ep_->drone_id_)) {
    return;
  }
  updatePeerTrust(static_cast<int>(msg->target_drone_id), msg->status);
  if (msg->status == TAKEOVER_COMPLETED && msg->failed_frontier_id >= 0) {
    c3_takeover_completed_until_wall_s_[msg->failed_frontier_id] =
        ros::WallTime::now().toSec() + c3_takeover_completed_cooldown_s_;
    std::ostringstream completed_fields;
    completed_fields << std::setprecision(17)
                     << "\"frontier_id\":" << msg->failed_frontier_id
                     << ",\"map_version\":" << currentMapVersion()
                     << ",\"cooldown_s\":" << c3_takeover_completed_cooldown_s_
                     << ",\"completed_until_wall_s\":"
                     << c3_takeover_completed_until_wall_s_[msg->failed_frontier_id];
    logTelemetryEvent("c3_takeover_completed_invalidate", completed_fields.str());
  }
  if (peer_handoff_pending_ && peer_handoff_published_ &&
      msg->owner_plan_seq == peer_handoff_plan_seq_ &&
      msg->failed_frontier_id == peer_handoff_frontier_id_ &&
      msg->target_drone_id ==
          static_cast<uint8_t>(peer_handoff_best_peer_id_)) {
    peer_handoff_receipt_status_ = msg->status_name;
    const bool terminal_receipt =
        msg->status == TAKEOVER_COMPLETED ||
        msg->status == TAKEOVER_ABORTED ||
        msg->status == TAKEOVER_STALE ||
        msg->status == TAKEOVER_REJECTED;
    if (terminal_receipt) {
      peer_handoff_observed_ = true;
    }
    logTelemetryEvent("peer_handoff_receipt",
        "\"status\":\"" + msg->status_name +
            "\",\"reason\":\"" + msg->reason +
            "\",\"request_id\":" + std::to_string(msg->request_id));
  }
}

void C2ExplorationManager::sendPeerTakeoverReceipt(
    const std::uint8_t owner_drone_id, const std::uint64_t request_id,
    const std::uint64_t owner_plan_seq, const int frontier_id,
    const Vector3d& goal, const double goal_yaw, const double issued_wall_s,
    const std::uint8_t status, const std::string& status_name,
    const std::string& reason) {
  if (!peer_takeover_receipt_pub_ || !ep_) return;
  exploration_manager::PeerTakeoverReceipt msg;
  msg.request_id = request_id;
  msg.owner_drone_id = owner_drone_id;
  msg.target_drone_id = static_cast<uint8_t>(ep_->drone_id_);
  msg.owner_plan_seq = owner_plan_seq;
  msg.failed_frontier_id = frontier_id;
  msg.status = status;
  msg.status_name = status_name;
  msg.reason = reason;
  msg.goal_x = goal.x();
  msg.goal_y = goal.y();
  msg.goal_z = goal.z();
  msg.goal_yaw = goal_yaw;
  msg.issued_wall_s = issued_wall_s;
  msg.receipt_wall_s = ros::WallTime::now().toSec();
  peer_takeover_receipt_pub_.publish(msg);

  std::ostringstream fields;
  fields << std::setprecision(17)
         << "\"request_id\":" << msg.request_id
         << ",\"owner_drone_id\":" << static_cast<int>(msg.owner_drone_id)
         << ",\"owner_plan_seq\":" << msg.owner_plan_seq
         << ",\"failed_frontier_id\":" << msg.failed_frontier_id
         << ",\"status\":\"" << msg.status_name << "\""
         << ",\"reason\":\"" << msg.reason << "\""
         << ",\"goal_x\":" << msg.goal_x
         << ",\"goal_y\":" << msg.goal_y
         << ",\"goal_z\":" << msg.goal_z;
  logTelemetryEvent("peer_takeover_receipt_sent", fields.str());
}

bool C2ExplorationManager::hasPendingPeerHandoff() {
  if (!peer_handoff_pending_) return false;
  const double now_wall_s = ros::WallTime::now().toSec();
  if (currentMapVersion() != peer_handoff_map_version_) {
    peer_handoff_fallback_reason_ = "map_version_changed";
    logTelemetryEvent("peer_handoff_fallback",
        "\"reason\":\"map_version_changed\"");
    clearPeerHandoff();
    return false;
  }

  if (!peer_handoff_published_) {
    if (now_wall_s >= peer_handoff_deadline_wall_s_) {
      peer_handoff_fallback_reason_ = "certificate_timeout";
      logTelemetryEvent("peer_handoff_fallback",
          "\"reason\":\"certificate_timeout\"");
      clearPeerHandoff();
      return false;
    }
    if (now_wall_s < peer_handoff_cert_window_end_wall_s_) {
      return true;
    }

    peer_handoff_owner_stuck_cost_s_ = estimateOwnerStuckCostS();
    peer_handoff_best_peer_id_ = selectBestPeerCertificate();
    if (peer_handoff_best_peer_id_ > 0) {
      peer_handoff_selected_cost_s_ = 0.0;
      peer_handoff_selected_benefit_s_ = 0.0;
      for (const auto& cert : pending_peer_certificates_) {
        if (cert.peer_id == peer_handoff_best_peer_id_) {
          peer_handoff_selected_cost_s_ = cert.peer_marginal_cost_s;
          peer_handoff_selected_benefit_s_ = cert.expected_benefit_s;
          break;
        }
      }
      std::ostringstream c3_fields;
      c3_fields << std::setprecision(17)
                << "\"peer_id\":" << peer_handoff_best_peer_id_
                << ",\"owner_stuck_cost_s\":"
                << peer_handoff_owner_stuck_cost_s_
                << ",\"peer_marginal_cost_s\":"
                << peer_handoff_selected_cost_s_
                << ",\"expected_benefit_s\":"
                << peer_handoff_selected_benefit_s_
                << ",\"trust_threshold\":" << c3_trust_threshold_
                << ",\"benefit_margin_s\":" << c3_benefit_margin_s_
                << ",\"failure_repeat_count\":" << prctFailureRepeatCount(
                    peer_handoff_frontier_id_, peer_handoff_goal_);
      logTelemetryEvent("c3_handoff_decision", c3_fields.str());
      publishPeerTakeoverGoal(peer_handoff_best_peer_id_, peer_handoff_plan_seq_,
          peer_handoff_frontier_id_, peer_handoff_goal_, peer_handoff_goal_yaw_);
      if (peer_handoff_published_) {
        logTelemetryEvent("peer_handoff_published",
            "\"peer_id\":" + std::to_string(peer_handoff_best_peer_id_) +
                ",\"request_id\":" +
                std::to_string(peer_handoff_request_id_));
        return true;
      }
      return false;
    }

    int best_candidate_id = -1;
    double best_candidate_benefit = -std::numeric_limits<double>::infinity();
    double best_candidate_cost = std::numeric_limits<double>::infinity();
    double best_candidate_path = std::numeric_limits<double>::infinity();
    double best_candidate_age = std::numeric_limits<double>::infinity();
    double best_candidate_load = std::numeric_limits<double>::infinity();
    double best_candidate_trust = 0.0;
    for (const auto& cert : pending_peer_certificates_) {
      if (cert.peer_id <= 0) continue;
      const bool better =
          best_candidate_id < 0 ||
          cert.expected_benefit_s > best_candidate_benefit + 1e-9 ||
          (std::abs(cert.expected_benefit_s - best_candidate_benefit) <= 1e-9 &&
           (cert.path_length_m < best_candidate_path - 0.05 ||
            (std::abs(cert.path_length_m - best_candidate_path) <= 0.05 &&
             cert.peer_state_age_s < best_candidate_age - 1e-6) ||
            (std::abs(cert.path_length_m - best_candidate_path) <= 0.05 &&
             std::abs(cert.peer_state_age_s - best_candidate_age) <= 1e-6 &&
             cert.peer_load < best_candidate_load - 1e-6)));
      if (better) {
        best_candidate_id = cert.peer_id;
        best_candidate_benefit = cert.expected_benefit_s;
        best_candidate_cost = cert.peer_marginal_cost_s;
        best_candidate_path = cert.path_length_m;
        best_candidate_age = cert.peer_state_age_s;
        best_candidate_load = cert.peer_load;
        best_candidate_trust = cert.peer_trust;
      }
    }

    const bool has_certificate = !pending_peer_certificates_.empty();
    peer_handoff_fallback_reason_ =
        c3_enable_marginal_gate_ && has_certificate
            ? "no_benefit_winner" : "no_peer_certificate";
    std::ostringstream c3_fields;
    c3_fields << std::setprecision(17)
              << "\"reason\":\"" << peer_handoff_fallback_reason_ << "\""
              << ",\"best_candidate_peer_id\":" << best_candidate_id
              << ",\"owner_stuck_cost_s\":" << peer_handoff_owner_stuck_cost_s_
              << ",\"best_peer_marginal_cost_s\":" << best_candidate_cost
              << ",\"best_peer_benefit_s\":" << best_candidate_benefit
              << ",\"best_peer_trust\":" << best_candidate_trust
              << ",\"best_peer_path_m\":" << best_candidate_path
              << ",\"best_peer_state_age_s\":" << best_candidate_age
              << ",\"best_peer_load\":" << best_candidate_load
              << ",\"trust_threshold\":" << c3_trust_threshold_
              << ",\"benefit_margin_s\":" << c3_benefit_margin_s_
              << ",\"failure_repeat_count\":" << prctFailureRepeatCount(
                  peer_handoff_frontier_id_, peer_handoff_goal_);
    logTelemetryEvent("c3_handoff_decision", c3_fields.str());
    if (has_certificate) {
      registerPrctTakeover(peer_handoff_frontier_id_, peer_handoff_goal_);
    }
    logTelemetryEvent("peer_handoff_fallback",
        "\"reason\":\"" + peer_handoff_fallback_reason_ + "\"");
    clearPeerHandoff();
    return false;
  }

  if (peer_handoff_observed_) {
    logTelemetryEvent("peer_handoff_receipt_complete",
        "\"status\":\"" + peer_handoff_receipt_status_ + "\"");
    clearPeerHandoff();
    return false;
  }
  if (now_wall_s >= peer_handoff_deadline_wall_s_) {
    peer_handoff_fallback_reason_ = "receipt_timeout";
    logTelemetryEvent("peer_handoff_fallback",
        "\"reason\":\"receipt_timeout\"");
    clearPeerHandoff();
    return false;
  }
  return true;
}

double C2ExplorationManager::peerHandoffWaitRemainingS() const {
  if (!peer_handoff_pending_) return 0.0;
  const double remaining =
      peer_handoff_deadline_wall_s_ - ros::WallTime::now().toSec();
  return std::max(0.0, remaining);
}

void C2ExplorationManager::clearPeerHandoff() {
  peer_handoff_pending_ = false;
  peer_handoff_published_ = false;
  peer_handoff_observed_ = false;
  peer_handoff_plan_seq_ = 0;
  peer_handoff_frontier_id_ = -1;
  peer_handoff_goal_.setZero();
  peer_handoff_goal_yaw_ = 0.0;
  peer_handoff_started_wall_s_ = 0.0;
  peer_handoff_best_peer_id_ = -1;
  peer_handoff_deadline_wall_s_ = 0.0;
  peer_handoff_cert_window_end_wall_s_ = 0.0;
  peer_handoff_receipt_status_.clear();
  peer_handoff_fallback_reason_.clear();
  peer_handoff_request_id_ = 0;
  peer_handoff_map_version_ = 0;
  peer_handoff_owner_stuck_cost_s_ = 0.0;
  peer_handoff_selected_benefit_s_ = 0.0;
  peer_handoff_selected_cost_s_ = 0.0;
  pending_peer_certificates_.clear();
}

int C2ExplorationManager::updateFrontierStruct(const Eigen::Vector3d& pos) {

  auto t1 = ros::Time::now();
  auto t2 = t1;
  ed_->views_.clear();
  ed_->views1_.clear();
  ed_->views2_.clear();

  // Search frontiers and group them into clusters
  frontier_finder_->searchFrontiers();

  double frontier_time = (ros::Time::now() - t1).toSec();
  t1 = ros::Time::now();

  // Find viewpoints (x,y,z,yaw) for all clusters; find the informative ones
  frontier_finder_->computeFrontiersToVisit();

  // Retrieve the updated info
  frontier_finder_->getFrontiers(ed_->frontiers_);
  frontier_finder_->getDormantFrontiers(ed_->dead_frontiers_);
  frontier_finder_->getFrontierBoxes(ed_->frontier_boxes_);

  frontier_finder_->getTopViewpointsInfo(
      pos, ed_->points_, ed_->yaws_, ed_->averages_, ed_->viewpoints_);

  // Advance a coarse PRCT task-map epoch only when the frontier viewpoint set changes.
  std::uint64_t summary = 1469598103934665603ULL;
  for (const auto& p : ed_->points_) {
    const std::int64_t xi = static_cast<std::int64_t>(std::round(p.x() * 10.0));
    const std::int64_t yi = static_cast<std::int64_t>(std::round(p.y() * 10.0));
    const std::int64_t zi = static_cast<std::int64_t>(std::round(p.z() * 10.0));
    summary = (summary ^ static_cast<std::uint64_t>(xi)) * 1099511628211ULL;
    summary = (summary ^ static_cast<std::uint64_t>(yi)) * 1099511628211ULL;
    summary = (summary ^ static_cast<std::uint64_t>(zi)) * 1099511628211ULL;
  }
  if (summary != prct_epoch_last_hash_) {
    prct_epoch_last_hash_ = summary;
    ++prct_map_epoch_;
    const uint64_t new_epoch = prct_map_epoch_;
    for (auto it = prct_cooldowns_.begin(); it != prct_cooldowns_.end();) {
      if (it->second.map_version != new_epoch) {
        it = prct_cooldowns_.erase(it);
      } else {
        ++it;
      }
    }
    for (auto it = prct_frontier_cooldowns_.begin();
         it != prct_frontier_cooldowns_.end();) {
      if (it->second.map_version != new_epoch) {
        it = prct_frontier_cooldowns_.erase(it);
      } else {
        ++it;
      }
    }
  }

  for (int i = 0; i < ed_->points_.size(); ++i) {
    ed_->views_.push_back(
        ed_->points_[i] + 2.0 * Vector3d(cos(ed_->yaws_[i]), sin(ed_->yaws_[i]), 0));

    vector<Vector3d> v1, v2;
    frontier_finder_->percep_utils_->setPose(ed_->points_[i], ed_->yaws_[i]);
    frontier_finder_->percep_utils_->getFOV(v1, v2);
    ed_->views1_.insert(ed_->views1_.end(), v1.begin(), v1.end());
    ed_->views2_.insert(ed_->views2_.end(), v2.begin(), v2.end());
  }

  if (ed_->frontiers_.empty()) {
    ROS_WARN("No coverable frontier.");
    return 0;
  }

  double view_time = (ros::Time::now() - t1).toSec();

  t1 = ros::Time::now();
  frontier_finder_->updateFrontierCostMatrix();

  double mat_time = (ros::Time::now() - t1).toSec();
  double total_time = frontier_time + view_time + mat_time;
  ROS_INFO("\033[1m\033[38;5;214m[Drone %d] frontier t: %lf, viewpoint t: %lf, mat t: %lf, total "
           "t: %lf\033[0m",
      ep_->drone_id_, frontier_time, view_time, mat_time, total_time);

  return ed_->frontiers_.size();
}

void C2ExplorationManager::findGridAndFrontierPath(const Vector3d& cur_pos,
    const Vector3d& cur_vel, const Vector3d& cur_yaw, vector<int>& grid_ids,
    vector<int>& frontier_ids) {
  auto t1 = ros::Time::now();

  // Select nearby drones according to their states' stamp
  vector<Eigen::Vector3d> positions = { cur_pos };
  // vector<Eigen::Vector3d> velocities = { Eigen::Vector3d(0, 0, 0) };
  vector<Eigen::Vector3d> velocities = { cur_vel };
  vector<double> yaws = { cur_yaw[0] };

  // Partitioning-based tour planning
  vector<int> ego_ids;  // hgrid sequence.
  vector<vector<int>> other_ids;
  // Coarse-grained selection of which grids to visit and in what order.
  if (!findGlobalTourOfGrid(positions, velocities, ego_ids, other_ids)) {
    grid_ids = {};
    return;
  }
  grid_ids = ego_ids;

  double grid_time = (ros::Time::now() - t1).toSec();

  // Frontier-based single drone tour planning
  // Restrict frontier within the first visited grid
  t1 = ros::Time::now();

  vector<int> ftr_ids;
  bool used_center_hull = false;
  bool used_center_grid = false;
  int best_task_idx = -1;

  // If current task token is a split hull, frontier tour must use frontiers in that hull.
  auto& ego_state = ed_->swarm_state_[ep_->drone_id_ - 1];
  if (ed_->region_tour_.size() > 1 && !ego_state.center_positions_.empty()) {
    double best_d2 = std::numeric_limits<double>::infinity();
    for (int i = 0; i < static_cast<int>(ego_state.center_positions_.size()); ++i) {
      const double d2 = (ego_state.center_positions_[i] - ed_->region_tour_[1]).squaredNorm();
      if (d2 < best_d2) {
        best_d2 = d2;
        best_task_idx = i;
      }
    }

    if (best_task_idx >= 0 && best_task_idx < static_cast<int>(ego_state.center_hulls_.size()) &&
        !ego_state.center_hulls_[best_task_idx].empty()) {
      const auto& task_hull = ego_state.center_hulls_[best_task_idx];
      for (int i = 0; i < static_cast<int>(ed_->averages_.size()); ++i) {
        if (pointInsideConvexHull2D(task_hull, ed_->averages_[i])) {
          ftr_ids.push_back(i);
        }
      }
      used_center_hull = true;
    }
  }

  if (!used_center_hull && ed_->region_tour_.size() > 1) {
    int center_grid_id = -1;
    if (hgrid_->getGridIdByCenterPos(ed_->region_tour_[1], center_grid_id)) {
      hgrid_->getFrontiersInGrid(vector<int>{ center_grid_id }, ftr_ids);
      used_center_grid = true;
    }
  }
  // For hull task, keep hull-only frontier set. For grid task, fallback to grid-level collection.
  if (!used_center_hull && ftr_ids.empty()) {
    hgrid_->getFrontiersInGrid(ego_ids, ftr_ids);
  }
  ROS_INFO(
      "\033[1m\033[38;5;117mFind frontier tour, %d involved------------\033[0m", ftr_ids.size());
  if (used_center_hull) {
    ROS_INFO("[Hull->Frontier] hull-aligned frontier extraction enabled");
  }
  if (used_center_grid) {
    ROS_INFO("[Grid->Frontier] center-aligned frontier extraction enabled");
  }

  // Consider next grid in frontier tour planning
  Eigen::Vector3d grid_pos;
  double grid_yaw;
  vector<Eigen::Vector3d> grid_pos_vec;
  if (ed_->region_tour_.size() > 1) {
    grid_pos_vec = { ed_->region_tour_[1] };
  } else if (hgrid_->getNextGrid(ego_ids, grid_pos, grid_yaw)) {
    grid_pos_vec = { grid_pos };
  }

  if (!ftr_ids.empty()) {
    findTourOfFrontier(cur_pos, cur_vel, cur_yaw, ftr_ids, grid_pos_vec, frontier_ids);
  } else {
    // No frontier; keep frontier_ids empty.
    frontier_ids.clear();
  }
  double ftr_time = (ros::Time::now() - t1).toSec();
  ROS_INFO("Grid tour t: %lf, frontier tour t: %lf.", grid_time, ftr_time);
}

void C2ExplorationManager::shortenPath(vector<Vector3d>& path) {
  if (path.empty()) {
    ROS_ERROR("Empty path to shorten");
    return;
  }
  // Shorten the tour, only critical intermediate points are reserved.
  const double dist_thresh = 3.0;
  vector<Vector3d> short_tour = { path.front() };
  for (int i = 1; i < path.size() - 1; ++i) {
    if ((path[i] - short_tour.back()).norm() > dist_thresh)
      short_tour.push_back(path[i]);
    else {
      // Add waypoints to shorten path only to avoid collision
      ViewNode::caster_->input(short_tour.back(), path[i + 1]);
      Eigen::Vector3i idx;
      while (ViewNode::caster_->nextId(idx) && ros::ok()) {
        if (edt_environment_->sdf_map_->getInflateOccupancy(idx) == 1 ||
            edt_environment_->sdf_map_->getOccupancy(idx) == SDFMap::UNKNOWN) {
          short_tour.push_back(path[i]);
          break;
        }
      }
    }
  }
  if ((path.back() - short_tour.back()).norm() > 1e-3) short_tour.push_back(path.back());

  // Ensure at least three points in the path
  if (short_tour.size() == 2)
    short_tour.insert(short_tour.begin() + 1, 0.5 * (short_tour[0] + short_tour[1]));
  path = short_tour;
}

void C2ExplorationManager::allocateTasks(
    const AllocationRequest& request, AllocationResult& result) {
  const auto& positions = request.drone_positions;
  const auto& drone_ids = request.drone_ids;
  const auto& grid_ids = request.grid_ids;
  const auto* drone_states = request.drone_states;
  const auto& blocked_center_node_ids = request.blocked_center_node_ids;
  const auto& blocked_center_hulls = request.blocked_center_hulls;
  auto& all_centers = result.centers;
  auto& all_center_hulls = result.center_hulls;

  all_centers.clear();
  const int drone_num = positions.size();
  all_centers.resize(drone_num);
  all_center_hulls.clear();
  all_center_hulls.resize(drone_num);

  if (grid_ids.empty() || drone_num == 0) {
    const std::uint64_t allocation_seq = ++telemetry_allocation_seq_;
    telemetry_active_allocation_seq_ = allocation_seq;
    logTelemetryEvent("allocation_request",
        "\"allocation_seq\":" + std::to_string(allocation_seq) +
            ",\"grid_count\":" + std::to_string(grid_ids.size()) +
            ",\"drone_count\":" + std::to_string(drone_num) + ",\"empty\":true");
    return;
  }

  const std::uint64_t allocation_seq = ++telemetry_allocation_seq_;
  telemetry_active_allocation_seq_ = allocation_seq;
  logTelemetryEvent("allocation_request",
      "\"allocation_seq\":" + std::to_string(allocation_seq) +
          ",\"grid_count\":" + std::to_string(grid_ids.size()) +
          ",\"drone_count\":" + std::to_string(drone_num) + ",\"empty\":false");

  AllocationCandidateSet candidates;
  if (!buildAllocationCandidateSet(request, candidates)) {
    logTelemetryEvent("allocation_result", "\"allocation_seq\":" +
        std::to_string(allocation_seq) + ",\"success\":false,\"reason\":\"candidate_set\"");
    return;
  }

  const int full_center_num = candidates.full_center_num;
  Eigen::MatrixXd use_mat = candidates.mat;
  std::vector<int> use_center_grid_ids = candidates.center_grid_ids;
  std::vector<Eigen::Vector3d> use_center_positions = candidates.center_positions;
  std::vector<vector<Vector3d>> use_center_hulls = candidates.center_hulls;
  std::vector<int> use_center_ids = candidates.center_ids;
  std::vector<int> use_center_types = candidates.center_types;
  int use_center_num = static_cast<int>(use_center_positions.size());
  int blocked_filtered_num = use_center_num;

  // Restrict MeetingOpt candidates to centers that still correspond to participant-owned tasks.
  // Split tasks match by hull; normal tasks match by center id / grid id.
  if (drone_states != nullptr &&
      drone_states->size() == static_cast<size_t>(drone_num) && !use_center_positions.empty()) {
    std::vector<int> owned_center_idx;
    filterMeetingOptCentersByParticipantTasks(
        hgrid_, *drone_states, use_center_grid_ids, use_center_positions, use_center_ids,
        use_center_types, use_center_hulls, owned_center_idx);

    if (owned_center_idx.empty()) {
      ROS_WARN_STREAM_THROTTLE(1.0,
          "[MeetingOpt] No candidate centers survive participant-task filtering, pooled_centers="
              << use_center_positions.size());
      return;
    }

    if (owned_center_idx.size() < use_center_positions.size()) {
      // Keep the cost matrix rows/cols aligned with the filtered center metadata.
      keepCenterSubset(owned_center_idx, drone_num, use_mat, use_center_grid_ids,
          use_center_positions, use_center_hulls, use_center_ids, use_center_types);
    }
    blocked_filtered_num = static_cast<int>(use_center_positions.size());

    ROS_WARN_STREAM_THROTTLE(1.0,
        "[MeetingOpt] participant-task center filter: pooled=" << full_center_num
                                                            << " blocked_filtered="
                                                            << blocked_filtered_num
                                                            << " owned="
                                                            << use_center_positions.size());
    use_center_num = static_cast<int>(use_center_positions.size());
  }

  // Drop centers that are already blocked by another participant's center id or split-task hull.
  if (!blocked_center_node_ids.empty() || !blocked_center_hulls.empty()) {
    std::vector<int> sel_center_idx;
    sel_center_idx.reserve(use_center_num);
    unordered_set<int> blocked_node_id_set(
        blocked_center_node_ids.begin(), blocked_center_node_ids.end());
    for (int i = 0; i < use_center_num; ++i) {
      const int cid = (i < static_cast<int>(use_center_ids.size())) ? use_center_ids[i] : -1;
      const bool has_hull =
          (i < static_cast<int>(use_center_hulls.size()) && !use_center_hulls[i].empty());
      if (!has_hull && cid >= 0 && blocked_node_id_set.find(cid) != blocked_node_id_set.end())
        continue;
      if (candidateBlockedByHull(use_center_positions[i], blocked_center_hulls)) continue;
      sel_center_idx.push_back(i);
    }
    ROS_WARN_STREAM_THROTTLE(1.0, "[MeetingOpt] center filter: full=" << full_center_num
                                                                    << " blocked_node_ids="
                                                                    << blocked_center_node_ids.size()
                                                                    << " blocked_hulls="
                                                                    << blocked_center_hulls.size()
                                                                    << " remain="
                                                                    << sel_center_idx.size());
    if (sel_center_idx.empty()) {
      ROS_WARN_THROTTLE(1.0, "[MeetingOpt] No candidate centers after blocked-center filtering");
      return;
    }
    if (sel_center_idx.size() < static_cast<size_t>(use_center_num)) {
      // Apply the same subset to both the matrix and the center metadata before ACVRP.
      keepCenterSubset(sel_center_idx, drone_num, use_mat, use_center_grid_ids,
          use_center_positions, use_center_hulls, use_center_ids, use_center_types);
    }
    use_center_num = static_cast<int>(use_center_positions.size());
  } else {
    blocked_filtered_num = use_center_num;
  }

  if (use_center_positions.empty()) {
    return;
  }

  // C2's matrix entries are allocation-cost units, not measured time. Record the
  // candidate set for later mechanism auditing without changing the allocation.
  double drone_center_cost_min = std::numeric_limits<double>::infinity();
  double drone_center_cost_max = -std::numeric_limits<double>::infinity();
  double drone_center_cost_sum = 0.0;
  int drone_center_cost_count = 0;
  for (int drone_idx = 0; drone_idx < drone_num; ++drone_idx) {
    for (int center_idx = 0; center_idx < use_center_num; ++center_idx) {
      const double value = use_mat(1 + drone_idx, 1 + drone_num + center_idx);
      if (!std::isfinite(value)) continue;
      drone_center_cost_min = std::min(drone_center_cost_min, value);
      drone_center_cost_max = std::max(drone_center_cost_max, value);
      drone_center_cost_sum += value;
      ++drone_center_cost_count;
    }
  }
  std::ostringstream candidate_fields;
  candidate_fields << std::setprecision(17)
                   << "\"allocation_seq\":" << allocation_seq
                   << ",\"full_center_count\":" << full_center_num
                   << ",\"post_filter_center_count\":" << use_center_num
                   << ",\"drone_count\":" << drone_num
                   << ",\"drone_center_cost_count\":" << drone_center_cost_count;
  if (drone_center_cost_count > 0) {
    candidate_fields << ",\"drone_center_cost_min\":" << drone_center_cost_min
                     << ",\"drone_center_cost_max\":" << drone_center_cost_max
                     << ",\"drone_center_cost_mean\":"
                     << drone_center_cost_sum / drone_center_cost_count;
  }
  logTelemetryEvent("allocation_candidate_set", candidate_fields.str());

  // Anti-oscillation on center ownership:
  // Penalize reassigning a center to a non-owner, optionally reward owner retention.
  if (ep_->center_switch_penalty_ > 1e-6 || ep_->center_stay_bonus_ > 1e-6) {
    vector<int> row_drone_ids(drone_num, -1);
    if (drone_ids.size() == static_cast<size_t>(drone_num)) {
      row_drone_ids = drone_ids;
    } else {
      for (int i = 0; i < drone_num; ++i) row_drone_ids[i] = i + 1;
    }

    unordered_map<int, int> center_owner;
    center_owner.reserve(use_center_ids.size() * 2);
    for (int did = 0; did < static_cast<int>(ed_->swarm_state_.size()); ++did) {
      const int drone_id = did + 1;
      const auto& state = ed_->swarm_state_[did];
      for (int i = 0; i < static_cast<int>(state.center_ids_.size()); ++i) {
        if (i < static_cast<int>(state.center_hulls_.size()) && !state.center_hulls_[i].empty()) {
          continue;
        }
        const int cid = state.center_ids_[i];
        if (cid < 0) continue;
        auto it = center_owner.find(cid);
        if (it == center_owner.end()) {
          center_owner[cid] = drone_id;
        } else if (it->second != drone_id) {
          it->second = -1;
        }
      }
    }

    int penalized_edges = 0;
    int rewarded_edges = 0;
    for (int i = 0; i < drone_num; ++i) {
      const int drone_id = row_drone_ids[i];
      if (drone_id <= 0) continue;
      for (int j = 0; j < use_center_num; ++j) {
        if (j >= static_cast<int>(use_center_ids.size())) continue;
        if (j < static_cast<int>(use_center_hulls.size()) && !use_center_hulls[j].empty()) continue;
        const int cid = use_center_ids[j];
        if (cid < 0) continue;
        auto it = center_owner.find(cid);
        if (it == center_owner.end()) continue;
        const int owner = it->second;
        if (owner <= 0) continue;

        const int row = 1 + i;
        const int col = 1 + drone_num + j;
        double adjusted = use_mat(row, col);
        if (owner == drone_id) {
          if (ep_->center_stay_bonus_ > 1e-6) {
            adjusted = std::max(0.0, adjusted - ep_->center_stay_bonus_);
            ++rewarded_edges;
          }
        } else {
          adjusted += ep_->center_switch_penalty_;
          ++penalized_edges;
        }
        use_mat(row, col) = adjusted;
      }
    }

    ROS_WARN_STREAM_THROTTLE(1.0,
        "[MeetingOpt] ownership hysteresis applied: switch_penalty="
            << ep_->center_switch_penalty_ << " stay_bonus=" << ep_->center_stay_bonus_
            << " penalized_edges=" << penalized_edges << " rewarded_edges=" << rewarded_edges);
  }

  auto logAllocationTasks = [&](int drone_idx, int participant_drone_id) {
    if (drone_idx < 0 || drone_idx >= static_cast<int>(all_centers.size())) return;
    for (size_t task_idx = 0; task_idx < all_centers[drone_idx].size(); ++task_idx) {
      const Vector3d& assigned = all_centers[drone_idx][task_idx];
      int center_idx = -1;
      double best_distance = std::numeric_limits<double>::infinity();
      for (int candidate_idx = 0; candidate_idx < use_center_num; ++candidate_idx) {
        const double distance = (use_center_positions[candidate_idx] - assigned).norm();
        if (distance < best_distance) {
          best_distance = distance;
          center_idx = candidate_idx;
        }
      }
      const int center_id = center_idx >= 0 && center_idx < static_cast<int>(use_center_ids.size())
          ? use_center_ids[center_idx] : -1;
      const int grid_id = center_idx >= 0 && center_idx < static_cast<int>(use_center_grid_ids.size())
          ? use_center_grid_ids[center_idx] : -1;
      const int center_type = center_idx >= 0 && center_idx < static_cast<int>(use_center_types.size())
          ? use_center_types[center_idx] : -1;
      const bool is_hull = center_idx >= 0 && center_idx < static_cast<int>(use_center_hulls.size()) &&
          !use_center_hulls[center_idx].empty();
      std::ostringstream task_fields;
      task_fields << std::setprecision(17)
                  << "\"allocation_seq\":" << allocation_seq
                  << ",\"participant_drone_id\":" << participant_drone_id
                  << ",\"task_index\":" << task_idx
                  << ",\"task_center_id\":" << center_id
                  << ",\"task_grid_id\":" << grid_id
                  << ",\"task_center_type\":" << center_type
                  << ",\"task_is_hull\":" << (is_hull ? 1 : 0)
                  << ",\"task_match_distance_m\":" << best_distance
                  << ",\"task_x\":" << assigned.x()
                  << ",\"task_y\":" << assigned.y()
                  << ",\"task_z\":" << assigned.z();
      logTelemetryEvent("allocation_task", task_fields.str());
    }
  };

  if (use_center_num == 1) {
    const auto& pt = use_center_positions.front();
    int best_idx = 0;
    double best_cost = std::numeric_limits<double>::infinity();
    for (int i = 0; i < drone_num; ++i) {
      vector<Eigen::Vector3d> path;
      double cost =
          ViewNode::computeCost(positions[i], pt, 0, 0, Eigen::Vector3d(0, 0, 0), 0, path);
      if (cost < best_cost) {
        best_cost = cost;
        best_idx = i;
      }
    }
    all_centers[best_idx] = { pt };
    all_center_hulls[best_idx] = { use_center_hulls.front() };
    const int participant_drone_id =
        best_idx < static_cast<int>(drone_ids.size()) ? drone_ids[best_idx] : best_idx + 1;
    logAllocationTasks(best_idx, participant_drone_id);
    logTelemetryEvent("allocation_assignment",
        "\"allocation_seq\":" + std::to_string(allocation_seq) +
            ",\"participant_drone_id\":" + std::to_string(participant_drone_id) +
            ",\"assigned_center_count\":1,\"nominal_tour_cost\":" +
            std::to_string(best_cost) + ","
            "\"cost_unit\":\"c2_allocation_cost\"");
    logTelemetryEvent("allocation_result", "\"allocation_seq\":" +
        std::to_string(allocation_seq) + ",\"success\":true,\"assigned_center_count\":1");
    return;
  }

  const int center_num = use_center_num;
  const int dimension = use_mat.rows();

  int capacity = 0;
  vector<int> demands(center_num, 0);
  if (ep_->prob_type_ == 2) {
    const double demand_scale = 0.1;
    const int base_demand = 1;

    int total_demand = 0;
    int max_demand = 0;

    unordered_map<int, int> grid_to_index;
    grid_to_index.reserve(grid_ids.size());
    for (int i = 0; i < static_cast<int>(grid_ids.size()); ++i) {
      grid_to_index[grid_ids[i]] = i;
    }

    vector<int> grid_demand(grid_ids.size(), 0);
    for (int i = 0; i < static_cast<int>(grid_ids.size()); ++i) {
      int unum = hgrid_->getUnknownCellsNum(grid_ids[i]);
      int d = 0;
      if (unum > 0) {
        d = (int)std::ceil(unum * demand_scale);
        d = std::max(d, 1);
      }
      d += base_demand;
      grid_demand[i] = d;
    }

    unordered_map<int, int> grid_center_counts;
    grid_center_counts.reserve(center_num);
    for (int gid : use_center_grid_ids) {
      grid_center_counts[gid] += 1;
    }

    for (int c = 0; c < center_num; ++c) {
      int gid = use_center_grid_ids[c];
      auto it = grid_to_index.find(gid);
      if (it == grid_to_index.end()) {
        demands[c] = 1;
        total_demand += demands[c];
        max_demand = std::max(max_demand, demands[c]);
        continue;
      }
      int gidx = it->second;
      int cnt = std::max(1, grid_center_counts[gid]);
      int per = (int)std::ceil(static_cast<double>(grid_demand[gidx]) / cnt);
      per = std::max(per, 1);
      demands[c] = per;
      total_demand += per;
      max_demand = std::max(max_demand, per);
    }

    const double slack = 0.10;
    capacity = (int)std::ceil((double)total_demand / (double)drone_num * (1.0 + slack));
    capacity = std::max(capacity, max_demand);
    ROS_WARN("ACVRP total_demand=%d, cap=%d, drones=%d", total_demand, capacity, drone_num);
  }

  ofstream file(ep_->mtsp_dir_ + "/amtsp3_" + to_string(ep_->drone_id_) + ".atsp");
  file << "NAME : allocation\n";

  if (ep_->prob_type_ == 1)
    file << "TYPE : ATSP\n";
  else if (ep_->prob_type_ == 2)
    file << "TYPE : ACVRP\n";

  file << "DIMENSION : " + to_string(dimension) + "\n";
  file << "EDGE_WEIGHT_TYPE : EXPLICIT\n";
  file << "EDGE_WEIGHT_FORMAT : FULL_MATRIX\n";

  if (ep_->prob_type_ == 2) {
    file << "CAPACITY : " + to_string(capacity) + "\n";
    file << "VEHICLES : " + to_string(drone_num) + "\n";
  }

  file << "EDGE_WEIGHT_SECTION\n";
  int sanitized_cost_cnt = 0;
  for (int i = 0; i < dimension; ++i) {
    for (int j = 0; j < dimension; ++j) {
      int int_cost = toLkhEdgeWeight(use_mat(i, j), sanitized_cost_cnt);
      file << int_cost << " ";
    }
    file << "\n";
  }
  if (sanitized_cost_cnt > 0) {
    ROS_WARN("[MeetingOpt] Sanitized %d invalid/overflow cost entries before LKH", sanitized_cost_cnt);
  }

  if (ep_->prob_type_ == 2) {
    file << "DEMAND_SECTION\n";
    file << "1 0\n";
    for (int i = 0; i < drone_num; ++i) {
      file << to_string(i + 2) + " 0\n";
    }
    for (int i = 0; i < center_num; ++i) {
      file << to_string(i + 2 + drone_num) + " " + to_string(demands[i]) + "\n";
    }
    file << "DEPOT_SECTION\n";
    file << "1\n";
    file << "EOF";
  }

  file.close();

  file.open(ep_->mtsp_dir_ + "/amtsp3_" + to_string(ep_->drone_id_) + ".par");
  file << "SPECIAL\n";
  file << "PROBLEM_FILE = " + ep_->mtsp_dir_ + "/amtsp3_" + to_string(ep_->drone_id_) + ".atsp\n";
  if (ep_->prob_type_ == 1) {
    file << "SALESMEN = " << to_string(drone_num) << "\n";
    file << "MTSP_OBJECTIVE = MINMAX\n";
    file << "SEED = 1\n";
    file << "TRACE_LEVEL = 0\n";
  } else if (ep_->prob_type_ == 2) {
    file << "TRACE_LEVEL = 1\n";
    file << "SEED = 0\n";
  }
  file << "PRECISION = " << kLkhPrecision << "\n";
  file << "RUNS = 1\n";
  file << "TOUR_FILE = " + ep_->mtsp_dir_ + "/amtsp3_" + to_string(ep_->drone_id_) + ".tour\n";
  file.close();

  snapshotTelemetryFile(ep_->mtsp_dir_ + "/amtsp3_" + to_string(ep_->drone_id_) + ".atsp",
      "lkh_acvrp_alloc_" + to_string(allocation_seq) + ".atsp");
  snapshotTelemetryFile(ep_->mtsp_dir_ + "/amtsp3_" + to_string(ep_->drone_id_) + ".par",
      "lkh_acvrp_alloc_" + to_string(allocation_seq) + ".par");

  lkh_mtsp_solver::SolveMTSP srv;
  srv.request.prob = 3;
  const double lkh_start_wall_s = ros::WallTime::now().toSec();
  logTelemetryEvent("lkh_request", "\"problem\":\"ACVRP\",\"dimension\":" +
      std::to_string(dimension));
  if (!acvrp_client_.call(srv)) {
    ROS_ERROR("Fail to solve ACVRP.");
    logTelemetryEvent("lkh_result", "\"problem\":\"ACVRP\",\"success\":false,\"duration_wall_s\":" +
        std::to_string(ros::WallTime::now().toSec() - lkh_start_wall_s));
    return;
  }
  logTelemetryEvent("lkh_result", "\"problem\":\"ACVRP\",\"success\":true,\"duration_wall_s\":" +
      std::to_string(ros::WallTime::now().toSec() - lkh_start_wall_s));
  snapshotTelemetryFile(ep_->mtsp_dir_ + "/amtsp3_" + to_string(ep_->drone_id_) + ".tour",
      "lkh_acvrp_alloc_" + to_string(allocation_seq) + ".tour");

  ifstream fin(ep_->mtsp_dir_ + "/amtsp3_" + to_string(ep_->drone_id_) + ".tour");
  string res;
  vector<int> ids;
  while (getline(fin, res)) {
    if (res.compare("TOUR_SECTION") == 0) break;
  }
  while (getline(fin, res)) {
    int id = stoi(res);
    ids.push_back(id - 1);
    if (id == -1) break;
  }
  fin.close();

  vector<vector<int>> tours;
  parseMultiTours(ids, drone_num, dimension, tours);
  vector<double> nominal_tour_costs(drone_num, 0.0);

  for (const auto& tr : tours) {
    if (tr.empty()) continue;
    int drone_token = tr.front();
    if (drone_token <= 0 || drone_token > drone_num) continue;
    int drone_idx = drone_token - 1;
    unordered_set<int> seen;
    int previous_matrix_id = 1 + drone_idx;
    for (size_t i = 1; i < tr.size(); ++i) {
      int id = tr[i];
      int center_idx = id - 1 - drone_num;
      if (center_idx >= 0 && center_idx < static_cast<int>(use_center_positions.size())) {
        if (seen.insert(center_idx).second) {
          const int center_matrix_id = 1 + drone_num + center_idx;
          nominal_tour_costs[drone_idx] += use_mat(previous_matrix_id, center_matrix_id);
          previous_matrix_id = center_matrix_id;
          all_centers[drone_idx].push_back(use_center_positions[center_idx]);
          all_center_hulls[drone_idx].push_back(use_center_hulls[center_idx]);
        }
      }
    }
  }

  int assigned_center_count = 0;
  for (int drone_idx = 0; drone_idx < drone_num; ++drone_idx) {
    assigned_center_count += static_cast<int>(all_centers[drone_idx].size());
    const int participant_drone_id =
        (drone_idx < static_cast<int>(drone_ids.size())) ? drone_ids[drone_idx] : drone_idx + 1;
    std::ostringstream assignment_fields;
    assignment_fields << std::setprecision(17)
                      << "\"allocation_seq\":" << allocation_seq
                      << ",\"participant_drone_id\":" << participant_drone_id
                      << ",\"assigned_center_count\":" << all_centers[drone_idx].size()
                      << ",\"nominal_tour_cost\":" << nominal_tour_costs[drone_idx]
                      << ",\"cost_unit\":\"c2_allocation_cost\"";
    logTelemetryEvent("allocation_assignment", assignment_fields.str());
    logAllocationTasks(drone_idx, participant_drone_id);
  }
  logTelemetryEvent("allocation_result", "\"allocation_seq\":" +
      std::to_string(allocation_seq) + ",\"success\":true,\"assigned_center_count\":" +
      std::to_string(assigned_center_count));
}

bool C2ExplorationManager::buildAllocationCandidateSet(
    const AllocationRequest& request, AllocationCandidateSet& candidates) {
  vector<HGrid::HullTask> hull_tasks;
  collectAssignedHullTasks(hgrid_, request.drone_states, hull_tasks);

  if (!hull_tasks.empty()) {
    hgrid_->getAllocationCostMatrix(request.drone_positions, request.drone_velocities,
        request.grid_ids, candidates.mat, &hull_tasks);
  } else {
    hgrid_->getAllocationCostMatrix(
        request.drone_positions, request.drone_velocities, request.grid_ids, candidates.mat);
  }

  const auto& center_grid_ids = hgrid_->getLastCostMatrixCenterGridIds();
  const auto& center_positions = hgrid_->getLastCostMatrixCenterPositions();
  const auto& center_hulls = hgrid_->getLastCostMatrixCenterHulls();
  const auto& center_ids = hgrid_->getLastCostMatrixCenterIds();
  const auto& center_types = hgrid_->getLastCostMatrixCenterTypes();

  candidates.full_center_num = static_cast<int>(center_grid_ids.size());
  if (candidates.full_center_num == 0) {
    return false;
  }

  candidates.center_grid_ids.clear();
  candidates.center_positions.clear();
  candidates.center_hulls.clear();
  candidates.center_ids.clear();
  candidates.center_types.clear();
  candidates.center_grid_ids.reserve(candidates.full_center_num);
  candidates.center_positions.reserve(candidates.full_center_num);
  candidates.center_hulls.reserve(candidates.full_center_num);
  candidates.center_ids.reserve(candidates.full_center_num);
  candidates.center_types.reserve(candidates.full_center_num);

  for (int i = 0; i < candidates.full_center_num; ++i) {
    candidates.center_grid_ids.push_back(center_grid_ids[i]);
    candidates.center_positions.push_back(center_positions[i]);
    if (i < static_cast<int>(center_hulls.size())) {
      candidates.center_hulls.push_back(center_hulls[i]);
    } else {
      candidates.center_hulls.emplace_back();
    }
    if (i < static_cast<int>(center_ids.size())) {
      candidates.center_ids.push_back(center_ids[i]);
    } else {
      candidates.center_ids.push_back(-1);
    }
    if (i < static_cast<int>(center_types.size())) {
      candidates.center_types.push_back(center_types[i]);
    } else {
      candidates.center_types.push_back(HGrid::FREE_ACTIVE_CENTER);
    }
  }

  return true;
}

bool C2ExplorationManager::findGlobalTourOfGrid(const vector<Eigen::Vector3d>& positions,
    const vector<Eigen::Vector3d>& velocities, vector<int>& indices, vector<vector<int>>& others,
    bool init) {

  ROS_INFO("\033[32mFind grid tour---------------\033[0m");

  auto t1 = ros::Time::now();

  auto& state = ed_->swarm_state_[ep_->drone_id_ - 1];
  vector<int> grid_ids = state.grid_ids_;
  int pre_grid_size = grid_ids.size();

  // print grid_ids
  std::cout << "\033[1;96mgrid_ids: ";
  for (auto id : grid_ids) std::cout << id << " ";
  std::cout << "\033[0m" << std::endl;

  // hgrid_->updateBaseCoor();  // Use the latest basecoor transform of swarm

  vector<int> first_ids, second_ids;
  hgrid_->inputFrontiers(ed_->averages_);

  hgrid_->updateGridData(ep_->drone_id_, grid_ids, ed_->reallocated_, ed_->last_grid_ids_,
      first_ids, second_ids, positions[0]);
  state.grid_ids_ = grid_ids;

  if (grid_ids.empty()) {
    ROS_WARN("Empty dominance.");
    ed_->region_tour_.clear();
    return false;
  }

  const auto& assigned_grids = state.grid_ids_;
  const auto& assigned_hulls = state.center_hulls_;
  vector<int> assigned_center_grid_ids = state.center_grid_ids_;
  if (assigned_center_grid_ids.size() < state.center_positions_.size()) {
    assigned_center_grid_ids.resize(state.center_positions_.size(), -1);
  }
  for (int i = 0; i < static_cast<int>(state.center_positions_.size()); ++i) {
    if (i < static_cast<int>(assigned_center_grid_ids.size()) &&
        assigned_center_grid_ids[i] >= 0)
      continue;
    int gid = -1;
    if (hgrid_->getGridIdByCenterPos(state.center_positions_[i], gid, 1.0)) {
      assigned_center_grid_ids[i] = gid;
    }
  }
  if (assigned_center_grid_ids != state.center_grid_ids_) {
    state.center_grid_ids_ = assigned_center_grid_ids;
  }

  vector<HGrid::HullTask> hull_tasks;
  const int hull_task_num = std::min(static_cast<int>(assigned_center_grid_ids.size()),
      static_cast<int>(state.center_hulls_.size()));
  hull_tasks.reserve(hull_task_num);
  for (int i = 0; i < hull_task_num; ++i) {
    if (assigned_center_grid_ids[i] < 0 || state.center_hulls_[i].empty()) continue;
    HGrid::HullTask task;
    task.grid_id = assigned_center_grid_ids[i];
    task.hull = state.center_hulls_[i];
    hull_tasks.push_back(std::move(task));
  }

  // Build the cost matrix.
  Eigen::MatrixXd mat_full;
  if (!hull_tasks.empty()) {
    hgrid_->getTourCostMatrix(positions, velocities, grid_ids, mat_full, &hull_tasks);
  } else {
    hgrid_->getTourCostMatrix(positions, velocities, grid_ids, mat_full);
  }

  const auto& full_center_grid_ids = hgrid_->getLastCostMatrixCenterGridIds();
  const auto& full_center_positions = hgrid_->getLastCostMatrixCenterPositions();
  const auto& full_center_hulls = hgrid_->getLastCostMatrixCenterHulls();
  const auto& full_center_ids = hgrid_->getLastCostMatrixCenterIds();
  const auto& full_center_types = hgrid_->getLastCostMatrixCenterTypes();
  Eigen::MatrixXd mat = mat_full;
  vector<int> center_grid_ids(full_center_grid_ids.begin(), full_center_grid_ids.end());
  vector<Vector3d> center_positions(full_center_positions.begin(), full_center_positions.end());

  const int task_num = std::max(
      static_cast<int>(state.center_positions_.size()),
      std::max(static_cast<int>(assigned_hulls.size()),
          static_cast<int>(assigned_center_grid_ids.size())));
  bool used_assigned_centers = false;
  if (task_num > 0) {
    unordered_map<int, int> assigned_grid_task_count;
    assigned_grid_task_count.reserve(task_num);
    for (int ti = 0; ti < task_num; ++ti) {
      const int gid = (ti < static_cast<int>(assigned_center_grid_ids.size()))
                          ? assigned_center_grid_ids[ti]
                          : -1;
      if (gid >= 0) {
        assigned_grid_task_count[gid] += 1;
      }
    }

    vector<int> selected_indices;
    vector<int> selected_task_indices;
    unordered_set<int> seen;
    selected_indices.reserve(task_num * 2);
    selected_task_indices.reserve(task_num * 2);

    unordered_map<int, vector<int>> grid_to_indices;
    grid_to_indices.reserve(full_center_grid_ids.size());
    for (int i = 0; i < static_cast<int>(full_center_grid_ids.size()); ++i) {
      grid_to_indices[full_center_grid_ids[i]].push_back(i);
    }

    auto chooseNearestByRef = [&](const vector<int>& candidates, const Vector3d& ref) {
      int best_idx = -1;
      double best_d2 = std::numeric_limits<double>::infinity();
      for (const int idx : candidates) {
        const double d2 = (full_center_positions[idx] - ref).squaredNorm();
        if (d2 < best_d2) {
          best_d2 = d2;
          best_idx = idx;
        }
      }
      return best_idx;
    };
    auto preferUnknownCenters = [&](const vector<int>& candidates) {
      vector<int> unknowns;
      unknowns.reserve(candidates.size());
      for (const int idx : candidates) {
        if (idx >= 0 && idx < static_cast<int>(full_center_types.size()) &&
            full_center_types[idx] == HGrid::UNKNOWN_ACTIVE_CENTER) {
          unknowns.push_back(idx);
        }
      }
      return unknowns.empty() ? candidates : unknowns;
    };

    int split_task_num = 0;
    int split_task_matched = 0;
    int split_task_finished = 0;
    unordered_set<int> split_locked_grids;
    split_locked_grids.reserve(task_num);

    // Task-token driven update: split tasks are solely tracked by their assigned hull.
    for (int ti = 0; ti < task_num; ++ti) {
      const bool has_hull = (ti < static_cast<int>(assigned_hulls.size()) &&
                             !assigned_hulls[ti].empty());
      const int task_gid = (ti < static_cast<int>(assigned_center_grid_ids.size()))
                               ? assigned_center_grid_ids[ti]
                               : -1;

      if (has_hull) {
        ++split_task_num;
        vector<int> contain_indices;
        contain_indices.reserve(4);
        for (int idx = 0; idx < static_cast<int>(full_center_grid_ids.size()); ++idx) {
          if (seen.find(idx) != seen.end()) continue;
          if (pointInsideConvexHull2D(assigned_hulls[ti], full_center_positions[idx])) {
            contain_indices.push_back(idx);
          }
        }

        if (!contain_indices.empty()) {
          const vector<int> preferred = preferUnknownCenters(contain_indices);
          const int matched_idx = chooseNearestByRef(preferred, hullCentroid(assigned_hulls[ti]));
          if (matched_idx >= 0 && seen.insert(matched_idx).second) {
            selected_indices.push_back(matched_idx);
            selected_task_indices.push_back(ti);
            ++split_task_matched;
          }
          if (task_gid >= 0) split_locked_grids.insert(task_gid);
        } else {
          // Hull task completion criterion:
          // if neither active UNKNOWN nor active FREE center falls inside this hull, this task is finished.
          ++split_task_finished;
        }
        continue;
      }

      // Non-split task: still represented by center-grid token.
      if (task_gid < 0) continue;
      if (split_locked_grids.find(task_gid) != split_locked_grids.end()) {
        // This grid already has a split-hull token; do not re-expand by grid token.
        continue;
      }
      auto it = grid_to_indices.find(task_gid);
      if (it == grid_to_indices.end()) continue;

      vector<int> cands;
      cands.reserve(it->second.size());
      for (const int idx : it->second) {
        if (seen.find(idx) == seen.end()) cands.push_back(idx);
      }
      if (cands.empty()) continue;
      const vector<int> preferred = preferUnknownCenters(cands);
      const bool grid_has_split_centers = hasExplicitSplitCenter(it->second, full_center_hulls);

      const bool can_expand_grid_token =
          (assigned_grid_task_count.count(task_gid) > 0 &&
           assigned_grid_task_count.at(task_gid) == 1 &&
           !grid_has_split_centers);
      if (can_expand_grid_token && preferred.size() > 1) {
        for (const int idx : preferred) {
          if (idx >= 0 && seen.insert(idx).second) {
            selected_indices.push_back(idx);
            selected_task_indices.push_back(ti);
          }
        }
        continue;
      }

      int selected = preferred.front();
      if (preferred.size() > 1 && ti < static_cast<int>(state.center_positions_.size())) {
        selected = chooseNearestByRef(preferred, state.center_positions_[ti]);
      }
      if (selected >= 0 && seen.insert(selected).second) {
        selected_indices.push_back(selected);
        selected_task_indices.push_back(ti);
      }
    }

    ROS_WARN_STREAM_THROTTLE(1.0, "[CenterFilter] Drone " << ep_->drone_id_
                                                           << " split_tasks=" << split_task_num
                                                           << " split_matched=" << split_task_matched
                                                           << " split_finished=" << split_task_finished
                                                           << " split_locked_grids="
                                                           << split_locked_grids.size());

    if (!selected_indices.empty()) {
      ROS_WARN_STREAM_THROTTLE(1.0, "[CenterFilter] Drone " << ep_->drone_id_
                                                             << " assigned_grids="
                                                             << assigned_grids.size()
                                                             << " assigned_hulls="
                                                             << assigned_hulls.size()
                                                             << " selected_centers="
                                                             << selected_indices.size()
                                                             << " full_centers="
                                                             << full_center_ids.size());
      const int drone_num = positions.size();
      const int sel_num = selected_indices.size();
      const int dimen = 1 + drone_num + sel_num;
      mat = Eigen::MatrixXd::Zero(dimen, dimen);
      // Copy depot/drone blocks
      for (int i = 0; i < 1 + drone_num; ++i) {
        for (int j = 0; j < 1 + drone_num; ++j) {
          mat(i, j) = mat_full(i, j);
        }
      }
      // Copy depot/drone <-> center and center <-> center blocks
      for (int i = 0; i < sel_num; ++i) {
        int full_i = 1 + drone_num + selected_indices[i];
        for (int j = 0; j < 1 + drone_num; ++j) {
          mat(j, 1 + drone_num + i) = mat_full(j, full_i);
          mat(1 + drone_num + i, j) = mat_full(full_i, j);
        }
        for (int j = 0; j < sel_num; ++j) {
          int full_j = 1 + drone_num + selected_indices[j];
          mat(1 + drone_num + i, 1 + drone_num + j) = mat_full(full_i, full_j);
        }
      }

      center_grid_ids.clear();
      center_positions.clear();
      center_grid_ids.reserve(sel_num);
      center_positions.reserve(sel_num);
      vector<int> selected_center_ids;
      vector<int> selected_center_grid_ids;
      vector<vector<Vector3d>> selected_center_hulls;
      selected_center_ids.reserve(sel_num);
      selected_center_grid_ids.reserve(sel_num);
      selected_center_hulls.reserve(sel_num);
      for (int i = 0; i < sel_num; ++i) {
        const int idx = selected_indices[i];
        const int ti = selected_task_indices[i];
        if (idx < 0 || idx >= static_cast<int>(full_center_grid_ids.size())) continue;
        center_grid_ids.push_back(full_center_grid_ids[idx]);
        center_positions.push_back(full_center_positions[idx]);
        if (idx < static_cast<int>(full_center_ids.size())) {
          selected_center_ids.push_back(full_center_ids[idx]);
        } else {
          selected_center_ids.push_back(-1);
        }
        if (ti < static_cast<int>(assigned_hulls.size()) && !assigned_hulls[ti].empty()) {
          // Preserve split-task hull token instead of replacing by refreshed full hull.
          selected_center_hulls.push_back(assigned_hulls[ti]);
        } else if (idx < static_cast<int>(full_center_hulls.size())) {
          selected_center_hulls.push_back(full_center_hulls[idx]);
        } else {
          selected_center_hulls.emplace_back();
        }
        if (ti < static_cast<int>(assigned_center_grid_ids.size()) &&
            assigned_center_grid_ids[ti] >= 0) {
          selected_center_grid_ids.push_back(assigned_center_grid_ids[ti]);
        } else {
          selected_center_grid_ids.push_back(full_center_grid_ids[idx]);
        }
      }
      state.center_positions_ = center_positions;
      state.center_hulls_ = selected_center_hulls;
      state.center_ids_ = selected_center_ids;
      state.center_grid_ids_ = selected_center_grid_ids;
    } else {
      ROS_WARN("[ATSP] Assigned centers not found from grid/hull after refresh, skip this round");
      // No assigned center survives after refresh:
      // clear stale grid/hull task tokens so finished hull tasks are retired immediately.
      state.center_ids_.clear();
      state.center_positions_.clear();
      state.center_hulls_.clear();
      state.center_grid_ids_.clear();
      indices.clear();
      ed_->region_tour_.clear();
      ed_->reallocated_ = false;
      return false;
    }
    used_assigned_centers = true;
  }

  int center_num = center_grid_ids.size();
  if (used_assigned_centers && center_num == 0) {
    // Strict ownership mode: do not expand from grid ids when assigned grid/hull cannot be matched.
    ROS_WARN("[ATSP] No matched assigned center by grid/hull after refresh, skip this planning round");
    indices.clear();
    ed_->region_tour_.clear();
    ed_->reallocated_ = false;
    return false;
  }
  if (center_num <= 1) {
    ed_->region_tour_.clear();
    ed_->region_tour_.push_back(positions[0]);
    if (!center_grid_ids.empty()) {
      indices = { center_grid_ids.front() };
      if (!center_positions.empty()) {
        ed_->region_tour_.push_back(center_positions.front());
      }
    } else {
      indices = grid_ids;
      if (used_assigned_centers && !center_positions.empty()) {
        for (const auto& c : center_positions) {
          ed_->region_tour_.push_back(c);
        }
      } else {
        for (auto gid : indices) {
          ed_->region_tour_.push_back(hgrid_->getCenter(gid));
        }
      }
    }
    ed_->last_grid_ids_ = indices;
    ed_->reallocated_ = false;
    ROS_WARN("[ATSP] Skip solver: <=1 center");
    return true;
  }

  double mat_time = (ros::Time::now() - t1).toSec();

  // Find optimal path through ATSP
  t1 = ros::Time::now();
  const int dimension = mat.rows();
  const int drone_num = 1;
  if (dimension < 3) {
    indices = grid_ids;
    ed_->region_tour_.clear();
    ed_->region_tour_.push_back(positions[0]);
    if (used_assigned_centers && !center_positions.empty()) {
      for (const auto& c : center_positions) {
        ed_->region_tour_.push_back(c);
      }
    } else {
      for (auto gid : indices) {
        ed_->region_tour_.push_back(hgrid_->getCenter(gid));
      }
    }
    ed_->last_grid_ids_ = indices;
    ed_->reallocated_ = false;
    return true;
  }

  // Create problem file
  ofstream file(ep_->mtsp_dir_ + "/amtsp2_" + to_string(ep_->drone_id_) + ".atsp");
  file << "NAME : amtsp\n";
  file << "TYPE : ATSP\n";
  file << "DIMENSION : " + to_string(dimension) + "\n";
  file << "EDGE_WEIGHT_TYPE : EXPLICIT\n";
  file << "EDGE_WEIGHT_FORMAT : FULL_MATRIX\n";
  file << "EDGE_WEIGHT_SECTION\n";
  int sanitized_cost_cnt = 0;
  for (int i = 0; i < dimension; ++i) {
    for (int j = 0; j < dimension; ++j) {
      int int_cost = toLkhEdgeWeight(mat(i, j), sanitized_cost_cnt);
      file << int_cost << " ";
    }
    file << "\n";
  }
  if (sanitized_cost_cnt > 0) {
    ROS_WARN("[GridTour] Sanitized %d invalid/overflow cost entries before LKH", sanitized_cost_cnt);
  }
  file.close();

  // Create par file
  file.open(ep_->mtsp_dir_ + "/amtsp2_" + to_string(ep_->drone_id_) + ".par");
  file << "SPECIAL\n";
  file << "PROBLEM_FILE = " + ep_->mtsp_dir_ + "/amtsp2_" + to_string(ep_->drone_id_) + ".atsp\n";
  file << "SALESMEN = " << to_string(drone_num) << "\n";
  file << "MTSP_OBJECTIVE = MINSUM\n";
  // file << "MTSP_MIN_SIZE = " << to_string(min(int(ed_->frontiers_.size()) / drone_num, 4)) <<
  // "\n"; file << "MTSP_MAX_SIZE = "
  //      << to_string(max(1, int(ed_->frontiers_.size()) / max(1, drone_num - 1))) << "\n";
  file << "PRECISION = " << kLkhPrecision << "\n";
  file << "RUNS = 1\n";
  file << "SEED = 1\n";
  file << "TRACE_LEVEL = 0\n";
  file << "TOUR_FILE = " + ep_->mtsp_dir_ + "/amtsp2_" + to_string(ep_->drone_id_) + ".tour\n";
  file.close();

  const std::uint64_t grid_snapshot_seq = ++telemetry_lkh_snapshot_seq_;
  snapshotTelemetryFile(ep_->mtsp_dir_ + "/amtsp2_" + to_string(ep_->drone_id_) + ".atsp",
      "lkh_grid_plan_" + to_string(grid_snapshot_seq) + ".atsp");
  snapshotTelemetryFile(ep_->mtsp_dir_ + "/amtsp2_" + to_string(ep_->drone_id_) + ".par",
      "lkh_grid_plan_" + to_string(grid_snapshot_seq) + ".par");

  auto par_dir = ep_->mtsp_dir_ + "/amtsp2_" + to_string(ep_->drone_id_) + ".atsp";
  t1 = ros::Time::now();

  lkh_mtsp_solver::SolveMTSP srv;
  srv.request.prob = 2;
  const double lkh_start_wall_s = ros::WallTime::now().toSec();
  logTelemetryEvent("lkh_request", "\"problem\":\"ATSP-grid\",\"dimension\":" +
      std::to_string(dimension));
  if (!tsp_client_.call(srv)) {
    ROS_ERROR("Fail to solve ATSP.");
    logTelemetryEvent("lkh_result", "\"problem\":\"ATSP-grid\",\"success\":false,\"duration_wall_s\":" +
        std::to_string(ros::WallTime::now().toSec() - lkh_start_wall_s));
    return false;
  }
  logTelemetryEvent("lkh_result", "\"problem\":\"ATSP-grid\",\"success\":true,\"duration_wall_s\":" +
      std::to_string(ros::WallTime::now().toSec() - lkh_start_wall_s));
  snapshotTelemetryFile(ep_->mtsp_dir_ + "/amtsp2_" + to_string(ep_->drone_id_) + ".tour",
      "lkh_grid_plan_" + to_string(grid_snapshot_seq) + ".tour");

  double mtsp_time = (ros::Time::now() - t1).toSec();
  // std::cout << "AmTSP time: " << mtsp_time << std::endl;

  // Read results
  t1 = ros::Time::now();

  ifstream fin(ep_->mtsp_dir_ + "/amtsp2_" + to_string(ep_->drone_id_) + ".tour");
  string res;
  vector<int> ids;
  while (getline(fin, res)) {
    if (res.compare("TOUR_SECTION") == 0) break;
  }
  while (getline(fin, res)) {
    int id = stoi(res);
    ids.push_back(id - 1);
    if (id == -1) break;
  }
  fin.close();

  // Parse the m-tour of grid
  vector<vector<int>> tours;
  vector<int> tour;
  for (auto id : ids) {
    if (id > 0 && id <= drone_num) {
      tour.clear();
      tour.push_back(id);
    } else if (id >= dimension || id <= 0) {
      tours.push_back(tour);
    } else {
      tour.push_back(id);
    }
  }

  // for (auto tr : tours) {
  //   std::cout << "tour: ";
  //   for (auto id : tr) std::cout << id << ", ";
  //   std::cout << "" << std::endl;
  // }
  others.resize(drone_num - 1);
  for (int i = 1; i < tours.size(); ++i) {
    if (tours[i][0] == 1) {
      indices.insert(indices.end(), tours[i].begin() + 1, tours[i].end());
    } else {
      others[tours[i][0] - 2].insert(
          others[tours[i][0] - 2].end(), tours[i].begin(), tours[i].end());
    }
  }
  bool fallback_to_grid_ids = false;
  if (indices.empty()) {
    ROS_WARN("[ATSP] Empty tour parsed, fallback to grid_ids");
    indices = grid_ids;
    fallback_to_grid_ids = true;
  }
  vector<int> center_order;
  if (!fallback_to_grid_ids) {
    center_order.reserve(indices.size());
    for (int id : indices) {
      int center_idx = id - 1 - drone_num;
      if (center_idx >= 0 && center_idx < static_cast<int>(center_positions.size())) {
        center_order.push_back(center_idx);
      }
    }
    for (auto& id : indices) {
      id -= 1 + drone_num;
      if (id >= 0 && id < static_cast<int>(center_grid_ids.size())) {
        id = center_grid_ids[id];
      }
    }
    for (auto& other : others) {
      for (auto& id : other) {
        id -= 1 + drone_num;
        if (id >= 0 && id < static_cast<int>(center_grid_ids.size())) {
          id = center_grid_ids[id];
        }
      }
    }
  }

  std::cout << "\033[1;32mGrid tour: ";
  if (!fallback_to_grid_ids) {
    unordered_set<int> seen;
    vector<int> unique_indices;
    for (int id : indices) {
      if (seen.insert(id).second) unique_indices.push_back(id);
    }
    indices.swap(unique_indices);
    for (auto& other : others) {
      seen.clear();
      vector<int> unique_other;
      for (int id : other) {
        if (seen.insert(id).second) unique_other.push_back(id);
      }
      other.swap(unique_other);
    }
  }
  for (auto& id : indices) {
    std::cout << id << " ";
  }
  std::cout << "\033[0m" << std::endl;

  ed_->region_tour_.clear();
  ed_->region_tour_.push_back(positions[0]);
  if (!center_order.empty()) {
    for (int center_idx : center_order) {
      if (center_idx >= 0 && center_idx < static_cast<int>(center_positions.size())) {
        ed_->region_tour_.push_back(center_positions[center_idx]);
      }
    }
  } else {
    unordered_map<int, int> first_center_idx;
    first_center_idx.reserve(center_grid_ids.size());
    for (int i = 0; i < static_cast<int>(center_grid_ids.size()); ++i) {
      if (first_center_idx.find(center_grid_ids[i]) == first_center_idx.end()) {
        first_center_idx[center_grid_ids[i]] = i;
      }
    }
    for (int gid : indices) {
      auto it = first_center_idx.find(gid);
      if (it != first_center_idx.end()) {
        ed_->region_tour_.push_back(center_positions[it->second]);
      } else {
        ed_->region_tour_.push_back(hgrid_->getCenter(gid));
      }
    }
  }

  grid_ids = indices;

  ed_->last_grid_ids_ = grid_ids;
  ed_->reallocated_ = false;


  return true;
}

void C2ExplorationManager::findTourOfFrontier(const Vector3d& cur_pos, const Vector3d& cur_vel,
    const Vector3d& cur_yaw, const vector<int>& ftr_ids, const vector<Eigen::Vector3d>& grid_pos,
    vector<int>& indices) {
  // if (ftr_ids.empty()) {
  //   indices.clear();
  //   return;
  // }

  auto t1 = ros::Time::now();
  indices.clear();

  // Filter out stale/invalid frontier ids propagated from grid cache.
  vector<int> valid_ftr_ids;
  valid_ftr_ids.reserve(ftr_ids.size());
  unordered_set<int> unique_ids;
  for (const int id : ftr_ids) {
    if (id < 0 || id >= static_cast<int>(ed_->frontiers_.size())) {
      ROS_WARN("[FrontierTour] Skip invalid frontier id %d (frontier size: %zu)", id,
          ed_->frontiers_.size());
      continue;
    }
    if (unique_ids.insert(id).second) {
      valid_ftr_ids.push_back(id);
    }
  }
  if (valid_ftr_ids.empty()) {
    ROS_WARN("[FrontierTour] No valid frontier id remains after filtering");
    return;
  }

  vector<Eigen::Vector3d> positions = { cur_pos };
  vector<Eigen::Vector3d> velocities = { cur_vel };
  vector<double> yaws = { cur_yaw[0] };

  // frontier_finder_->getSwarmCostMatrix(positions, velocities, yaws, mat);
  Eigen::MatrixXd mat;
  // getSwarmCostMatrix costs between frontiers.
  frontier_finder_->getSwarmCostMatrix(positions, velocities, yaws, valid_ftr_ids, grid_pos, mat);
  const int dimension = mat.rows();
  // std::cout << "dim of frontier TSP mat: " << dimension << std::endl;
  if (dimension < 3) {
    indices = valid_ftr_ids;
    return;
  }

  double mat_time = (ros::Time::now() - t1).toSec();
  // ROS_INFO("mat time: %lf", mat_time);

  // Find optimal allocation through AmTSP
  t1 = ros::Time::now();

  // Create problem file
  ofstream file(ep_->mtsp_dir_ + "/amtsp_" + to_string(ep_->drone_id_) + ".atsp");
  file << "NAME : amtsp\n";
  file << "TYPE : ATSP\n";
  file << "DIMENSION : " + to_string(dimension) + "\n";
  file << "EDGE_WEIGHT_TYPE : EXPLICIT\n";
  file << "EDGE_WEIGHT_FORMAT : FULL_MATRIX\n";
  file << "EDGE_WEIGHT_SECTION\n";
  int sanitized_cost_cnt = 0;
  for (int i = 0; i < dimension; ++i) {
    for (int j = 0; j < dimension; ++j) {
      int int_cost = toLkhEdgeWeight(mat(i, j), sanitized_cost_cnt);
      file << int_cost << " ";
    }
    file << "\n";
  }
  if (sanitized_cost_cnt > 0) {
    ROS_WARN(
        "[FrontierTour] Sanitized %d invalid/overflow cost entries before LKH", sanitized_cost_cnt);
  }
  file.close();

  // Create par file
  const int drone_num = 1;

  file.open(ep_->mtsp_dir_ + "/amtsp_" + to_string(ep_->drone_id_) + ".par");
  file << "SPECIAL\n";
  file << "PROBLEM_FILE = " + ep_->mtsp_dir_ + "/amtsp_" + to_string(ep_->drone_id_) + ".atsp\n";
  file << "SALESMEN = " << to_string(drone_num) << "\n";
  file << "MTSP_OBJECTIVE = MINSUM\n";
  file << "MTSP_MIN_SIZE = " << to_string(min(int(ed_->frontiers_.size()) / drone_num, 4)) << "\n";
  file << "MTSP_MAX_SIZE = "
       << to_string(max(1, int(ed_->frontiers_.size()) / max(1, drone_num - 1))) << "\n";
  file << "PRECISION = " << kLkhPrecision << "\n";
  file << "RUNS = 1\n";
  file << "SEED = 1\n";
  file << "TRACE_LEVEL = 0\n";
  file << "TOUR_FILE = " + ep_->mtsp_dir_ + "/amtsp_" + to_string(ep_->drone_id_) + ".tour\n";
  file.close();

  const std::uint64_t frontier_snapshot_seq = ++telemetry_lkh_snapshot_seq_;
  snapshotTelemetryFile(ep_->mtsp_dir_ + "/amtsp_" + to_string(ep_->drone_id_) + ".atsp",
      "lkh_frontier_plan_" + to_string(frontier_snapshot_seq) + ".atsp");
  snapshotTelemetryFile(ep_->mtsp_dir_ + "/amtsp_" + to_string(ep_->drone_id_) + ".par",
      "lkh_frontier_plan_" + to_string(frontier_snapshot_seq) + ".par");

  auto par_dir = ep_->mtsp_dir_ + "/amtsp_" + to_string(ep_->drone_id_) + ".atsp";
  t1 = ros::Time::now();

  lkh_mtsp_solver::SolveMTSP srv;
  srv.request.prob = 1;
  const double lkh_start_wall_s = ros::WallTime::now().toSec();
  logTelemetryEvent("lkh_request", "\"problem\":\"ATSP-frontier\",\"dimension\":" +
      std::to_string(dimension));
  if (!tsp_client_.call(srv)) {
    ROS_ERROR("Fail to solve ATSP.");
    logTelemetryEvent("lkh_result", "\"problem\":\"ATSP-frontier\",\"success\":false,\"duration_wall_s\":" +
        std::to_string(ros::WallTime::now().toSec() - lkh_start_wall_s));
    return;
  }
  logTelemetryEvent("lkh_result", "\"problem\":\"ATSP-frontier\",\"success\":true,\"duration_wall_s\":" +
      std::to_string(ros::WallTime::now().toSec() - lkh_start_wall_s));
  snapshotTelemetryFile(ep_->mtsp_dir_ + "/amtsp_" + to_string(ep_->drone_id_) + ".tour",
      "lkh_frontier_plan_" + to_string(frontier_snapshot_seq) + ".tour");

  double mtsp_time = (ros::Time::now() - t1).toSec();
  // ROS_INFO("AmTSP time: %lf", mtsp_time);

  // Read results
  t1 = ros::Time::now();

  ifstream fin(ep_->mtsp_dir_ + "/amtsp_" + to_string(ep_->drone_id_) + ".tour");
  if (!fin.is_open()) {
    ROS_ERROR("[FrontierTour] Failed to open tour file for parsing");
    return;
  }
  string res;
  vector<int> ids;
  bool found_tour_section = false;
  while (getline(fin, res)) {
    if (res.compare("TOUR_SECTION") == 0) {
      found_tour_section = true;
      break;
    }
  }
  if (!found_tour_section) {
    ROS_ERROR("[FrontierTour] TOUR_SECTION not found in mtsp tour file");
    fin.close();
    return;
  }
  while (getline(fin, res)) {
    int id = 0;
    try {
      id = stoi(res);
    } catch (const std::exception&) {
      continue;
    }
    ids.push_back(id - 1);
    if (id == -1) break;
  }
  fin.close();

  // Parse the m-tour
  vector<vector<int>> tours;
  parseMultiTours(ids, drone_num, dimension, tours);

  for (const auto& tour : tours) {
    if (tour.empty() || tour.front() != 1) continue;
    for (int i = 1; i < static_cast<int>(tour.size()); ++i) {
      const int local_id = tour[i] - (1 + drone_num);
      if (!grid_pos.empty() && local_id == static_cast<int>(valid_ftr_ids.size())) {
        // Virtual "next grid" node used by TSP refinement.
        continue;
      }
      if (local_id < 0 || local_id >= static_cast<int>(valid_ftr_ids.size())) {
        ROS_WARN("[FrontierTour] Skip parsed frontier index %d (valid size: %zu)", local_id,
            valid_ftr_ids.size());
        continue;
      }
      indices.push_back(valid_ftr_ids[local_id]);
    }
  }

  if (indices.empty()) {
    ROS_WARN("[FrontierTour] Empty parsed tour, fallback to filtered frontier ids");
    indices = valid_ftr_ids;
  }

  // Get the path of optimal tour from path matrix
  frontier_finder_->getPathForTour(cur_pos, indices, ed_->frontier_tour_);
  if (!grid_pos.empty()) {
    ed_->frontier_tour_.push_back(grid_pos[0]);
  }

  // ed_->other_tours_.clear();
  // for (int i = 1; i < positions.size(); ++i) {
  //   ed_->other_tours_.push_back({});
  //   frontier_finder_->getPathForTour(positions[i], others[i - 1], ed_->other_tours_[i - 1]);
  // }

  double parse_time = (ros::Time::now() - t1).toSec();
  // ROS_INFO("Cost mat: %lf, TSP: %lf, parse: %f, %d frontiers assigned.", mat_time, mtsp_time,
  //     parse_time, indices.size());
}

int C2ExplorationManager::toLkhEdgeWeight(const double raw_cost, int& sanitized_count) {
  double cost = raw_cost;
  if (!std::isfinite(cost) || cost < 0.0) {
    cost = kLkhFallbackBlockedCost;
    ++sanitized_count;
  }
  long long w = static_cast<long long>(std::llround(cost * kLkhCostScale));
  if (w < 0) {
    w = 0;
    ++sanitized_count;
  } else if (w > kLkhMaxEdgeWeight) {
    w = kLkhMaxEdgeWeight;
    ++sanitized_count;
  }
  return static_cast<int>(w);
}

void C2ExplorationManager::parseMultiTours(
    const vector<int>& ids, const int drone_num, const int dimension, vector<vector<int>>& tours) {
  tours.clear();
  vector<int> cur;
  vector<int> prefix;
  bool seen_salesman = false;
  for (int id : ids) {
    if (id <= 0) {
      continue;
    }
    if (id <= drone_num) {
      if (!cur.empty()) {
        tours.push_back(cur);
      }
      cur.clear();
      cur.push_back(id);
      seen_salesman = true;
      continue;
    }
    if (id >= dimension) {
      continue;
    }
    if (seen_salesman) {
      cur.push_back(id);
    } else {
      prefix.push_back(id);
    }
  }
  if (!cur.empty()) {
    tours.push_back(cur);
  }
  if (!prefix.empty() && !tours.empty()) {
    tours.back().insert(tours.back().end(), prefix.begin(), prefix.end());
  }
}

bool C2ExplorationManager::pointInsideConvexHull2D(
    const vector<Vector3d>& hull, const Vector3d& p, const double tol) {
  if (hull.empty()) return false;
  if (hull.size() == 1) {
    return (hull.front().head<2>() - p.head<2>()).norm() <= tol;
  }
  if (hull.size() == 2) {
    const auto a = hull[0].head<2>();
    const auto b = hull[1].head<2>();
    const auto ab = b - a;
    const double len = ab.norm();
    if (len < 1e-6) return (a - p.head<2>()).norm() <= tol;
    const double t = std::max(0.0, std::min(1.0, (p.head<2>() - a).dot(ab) / (len * len)));
    const auto proj = a + t * ab;
    return (proj - p.head<2>()).norm() <= tol;
  }

  int sign = 0;
  for (size_t i = 0; i < hull.size(); ++i) {
    const auto a = hull[i].head<2>();
    const auto b = hull[(i + 1) % hull.size()].head<2>();
    const auto e = b - a;
    const double el = e.norm();
    if (el < 1e-6) continue;
    const auto ap = p.head<2>() - a;
    const double signed_dist = (e.x() * ap.y() - e.y() * ap.x()) / el;
    if (std::abs(signed_dist) <= tol) continue;
    const int cur_sign = (signed_dist > 0.0) ? 1 : -1;
    if (sign == 0) {
      sign = cur_sign;
    } else if (sign != cur_sign) {
      return false;
    }
  }
  return true;
}

Vector3d C2ExplorationManager::hullCentroid(const vector<Vector3d>& hull) {
  Vector3d c = Vector3d::Zero();
  if (hull.empty()) return c;
  for (const auto& p : hull) c += p;
  c /= static_cast<double>(hull.size());
  return c;
}

bool C2ExplorationManager::hasExplicitSplitCenter(
    const vector<int>& candidate_indices, const vector<vector<Vector3d>>& center_hulls) {
  for (const int idx : candidate_indices) {
    if (idx >= 0 && idx < static_cast<int>(center_hulls.size()) && !center_hulls[idx].empty()) {
      return true;
    }
  }
  return false;
}

bool C2ExplorationManager::candidateBlockedByHull(
    const Vector3d& center, const vector<vector<Vector3d>>& blocked_center_hulls) {
  for (const auto& hull : blocked_center_hulls) {
    if (!hull.empty() && pointInsideConvexHull2D(hull, center)) return true;
  }
  return false;
}

void C2ExplorationManager::filterMeetingOptCentersByParticipantTasks(const shared_ptr<HGrid>& hgrid,
    const vector<DroneState>& participant_states, const vector<int>& center_grid_ids,
    const vector<Vector3d>& center_positions, const vector<int>& center_ids,
    const vector<int>& center_types, const vector<vector<Vector3d>>& center_hulls,
    vector<int>& selected_indices) {
  selected_indices.clear();
  if (!hgrid || participant_states.empty() || center_grid_ids.empty() || center_positions.empty()) {
    return;
  }

  struct NormalizedStateInfo {
    vector<int> task_gids;
    vector<int> extra_grid_ids;
    unordered_map<int, int> grid_task_count;
  };

  unordered_map<int, vector<int>> grid_to_indices;
  grid_to_indices.reserve(center_grid_ids.size());
  for (int i = 0; i < static_cast<int>(center_grid_ids.size()); ++i) {
    grid_to_indices[center_grid_ids[i]].push_back(i);
  }
  unordered_map<int, vector<int>> center_id_to_indices;
  center_id_to_indices.reserve(center_ids.size());
  for (int i = 0; i < static_cast<int>(center_ids.size()); ++i) {
    if (center_ids[i] >= 0) {
      center_id_to_indices[center_ids[i]].push_back(i);
    }
  }

  auto chooseNearestByRef = [&](const vector<int>& candidates, const Vector3d& ref) {
    int best_idx = -1;
    double best_d2 = std::numeric_limits<double>::infinity();
    for (const int idx : candidates) {
      const double d2 = (center_positions[idx] - ref).squaredNorm();
      if (d2 < best_d2) {
        best_d2 = d2;
        best_idx = idx;
      }
    }
    return best_idx;
  };

  auto preferUnknownCenters = [&](const vector<int>& candidates) {
    vector<int> unknowns;
    unknowns.reserve(candidates.size());
    for (const int idx : candidates) {
      if (idx >= 0 && idx < static_cast<int>(center_types.size()) &&
          center_types[idx] == HGrid::UNKNOWN_ACTIVE_CENTER) {
        unknowns.push_back(idx);
      }
    }
    return unknowns.empty() ? candidates : unknowns;
  };

  vector<NormalizedStateInfo> state_infos(participant_states.size());
  unordered_map<int, int> global_grid_task_count;
  global_grid_task_count.reserve(center_grid_ids.size() * 2);

  for (int si = 0; si < static_cast<int>(participant_states.size()); ++si) {
    const auto& state = participant_states[si];
    auto& info = state_infos[si];

    vector<int> assigned_center_grid_ids = state.center_grid_ids_;
    if (assigned_center_grid_ids.size() < state.center_positions_.size()) {
      assigned_center_grid_ids.resize(state.center_positions_.size(), -1);
    }
    for (int i = 0; i < static_cast<int>(state.center_positions_.size()); ++i) {
      if (i < static_cast<int>(assigned_center_grid_ids.size()) &&
          assigned_center_grid_ids[i] >= 0) {
        continue;
      }
      int gid = -1;
      if (hgrid->getGridIdByCenterPos(state.center_positions_[i], gid, 1.0)) {
        assigned_center_grid_ids[i] = gid;
      }
    }

    const int task_num = std::max(static_cast<int>(state.center_positions_.size()),
        std::max(static_cast<int>(state.center_hulls_.size()),
            static_cast<int>(assigned_center_grid_ids.size())));
    info.task_gids.resize(task_num, -1);
    unordered_set<int> covered_grid_tokens;
    covered_grid_tokens.reserve(task_num + state.grid_ids_.size());

    for (int ti = 0; ti < task_num; ++ti) {
      int task_gid = (ti < static_cast<int>(assigned_center_grid_ids.size()))
                         ? assigned_center_grid_ids[ti]
                         : -1;
      if (task_gid < 0 && ti < static_cast<int>(state.center_positions_.size())) {
        hgrid->getGridIdByCenterPos(state.center_positions_[ti], task_gid, 1.0);
      }
      info.task_gids[ti] = task_gid;
      if (task_gid < 0) continue;
      covered_grid_tokens.insert(task_gid);
      info.grid_task_count[task_gid] += 1;
      global_grid_task_count[task_gid] += 1;
    }

    for (const int gid : state.grid_ids_) {
      if (gid < 0 || covered_grid_tokens.find(gid) != covered_grid_tokens.end()) continue;
      info.extra_grid_ids.push_back(gid);
      info.grid_task_count[gid] += 1;
      global_grid_task_count[gid] += 1;
    }
  }

  unordered_set<int> seen;
  seen.reserve(center_positions.size());

  for (int si = 0; si < static_cast<int>(participant_states.size()); ++si) {
    const auto& state = participant_states[si];
    const auto& info = state_infos[si];
    const int task_num = info.task_gids.size();

    for (int ti = 0; ti < task_num; ++ti) {
      const bool has_ref_center = (ti < static_cast<int>(state.center_positions_.size()));
      const Vector3d ref_center =
          has_ref_center ? state.center_positions_[ti] : Vector3d::Zero();

      vector<Vector3d> task_hull;
      bool has_hull = false;
      if (ti < static_cast<int>(state.center_hulls_.size()) && !state.center_hulls_[ti].empty()) {
        task_hull = state.center_hulls_[ti];
        has_hull = true;
      } else if (has_ref_center) {
        has_hull = hgrid->getCenterHullByPos(ref_center, task_hull, 1.0) && !task_hull.empty();
      }

      const int task_gid = (ti < static_cast<int>(info.task_gids.size())) ? info.task_gids[ti] : -1;
      const int task_cid = (ti < static_cast<int>(state.center_ids_.size())) ? state.center_ids_[ti] : -1;
      const bool single_token_grid = (task_gid >= 0 &&
                                      info.grid_task_count.count(task_gid) > 0 &&
                                      info.grid_task_count.at(task_gid) == 1 &&
                                      global_grid_task_count.count(task_gid) > 0 &&
                                      global_grid_task_count.at(task_gid) == 1);

      if (has_hull) {
        vector<int> contain_indices;
        contain_indices.reserve(4);
        for (int idx = 0; idx < static_cast<int>(center_positions.size()); ++idx) {
          if (seen.find(idx) != seen.end()) continue;
          if (pointInsideConvexHull2D(task_hull, center_positions[idx])) {
            contain_indices.push_back(idx);
          }
        }

        if (contain_indices.empty()) continue;
        const vector<int> preferred = preferUnknownCenters(contain_indices);
        const Vector3d ref = has_ref_center ? ref_center : hullCentroid(task_hull);
        const int matched_idx = chooseNearestByRef(preferred, ref);
        if (matched_idx >= 0 && seen.insert(matched_idx).second) {
          selected_indices.push_back(matched_idx);
        }
        continue;
      }

      bool matched_exact_center = false;
      if (task_cid >= 0) {
        auto it = center_id_to_indices.find(task_cid);
        if (it != center_id_to_indices.end()) {
          vector<int> cands;
          cands.reserve(it->second.size());
          for (const int idx : it->second) {
            if (seen.find(idx) == seen.end()) cands.push_back(idx);
          }
          if (!cands.empty()) {
            const int matched_idx =
                (has_ref_center && cands.size() > 1) ? chooseNearestByRef(cands, ref_center) : cands.front();
            if (matched_idx >= 0 && seen.insert(matched_idx).second) {
              selected_indices.push_back(matched_idx);
              matched_exact_center = true;
            }
          }
        }
      }

      if (task_gid < 0) continue;
      auto it = grid_to_indices.find(task_gid);
      if (it == grid_to_indices.end()) continue;

      vector<int> cands;
      cands.reserve(it->second.size());
      for (const int idx : it->second) {
        if (seen.find(idx) == seen.end()) cands.push_back(idx);
      }
      if (cands.empty()) continue;

      const vector<int> preferred = preferUnknownCenters(cands);
      const bool grid_has_split_centers = hasExplicitSplitCenter(it->second, center_hulls);

      if (matched_exact_center) {
        continue;
      }

      if (single_token_grid && !grid_has_split_centers) {
        for (const int idx : preferred) {
          if (idx >= 0 && seen.insert(idx).second) {
            selected_indices.push_back(idx);
          }
        }
        continue;
      }

      int matched_idx = preferred.front();
      if (has_ref_center && preferred.size() > 1) {
        matched_idx = chooseNearestByRef(preferred, ref_center);
      }
      if (matched_idx >= 0 && seen.insert(matched_idx).second) {
        selected_indices.push_back(matched_idx);
      }
    }

    for (const int gid : info.extra_grid_ids) {
      if (gid < 0) continue;
      auto it = grid_to_indices.find(gid);
      if (it == grid_to_indices.end()) continue;

      vector<int> cands;
      cands.reserve(it->second.size());
      for (const int idx : it->second) {
        if (seen.find(idx) == seen.end()) cands.push_back(idx);
      }
      if (cands.empty()) continue;

      const vector<int> preferred = preferUnknownCenters(cands);
      const bool single_token_grid = (info.grid_task_count.count(gid) > 0 &&
                                      info.grid_task_count.at(gid) == 1 &&
                                      global_grid_task_count.count(gid) > 0 &&
                                      global_grid_task_count.at(gid) == 1);
      const bool grid_has_split_centers = hasExplicitSplitCenter(it->second, center_hulls);
      if (grid_has_split_centers) {
        // Bare grid tokens are not allowed to claim arbitrary sibling split parts.
        continue;
      }
      if (single_token_grid) {
        for (const int idx : preferred) {
          if (idx >= 0 && seen.insert(idx).second) {
            selected_indices.push_back(idx);
          }
        }
        continue;
      }

      const int matched_idx = preferred.front();
      if (matched_idx >= 0 && seen.insert(matched_idx).second) {
        selected_indices.push_back(matched_idx);
      }
    }
  }
}

void C2ExplorationManager::collectAssignedHullTasks(
    const shared_ptr<HGrid>& hgrid, const vector<DroneState>* states,
    vector<HGrid::HullTask>& hull_tasks) {
  hull_tasks.clear();
  if (!hgrid || states == nullptr) return;
  for (const auto& state : *states) {
    vector<int> assigned_center_grid_ids = state.center_grid_ids_;
    if (assigned_center_grid_ids.size() < state.center_positions_.size()) {
      assigned_center_grid_ids.resize(state.center_positions_.size(), -1);
    }
    for (int i = 0; i < static_cast<int>(state.center_positions_.size()); ++i) {
      if (assigned_center_grid_ids[i] >= 0) continue;
      int gid = -1;
      if (hgrid->getGridIdByCenterPos(state.center_positions_[i], gid, 1.0)) {
        assigned_center_grid_ids[i] = gid;
      }
    }

    const int task_num = std::min(static_cast<int>(assigned_center_grid_ids.size()),
        static_cast<int>(state.center_hulls_.size()));
    for (int i = 0; i < task_num; ++i) {
      if (assigned_center_grid_ids[i] < 0 || state.center_hulls_[i].empty()) continue;
      HGrid::HullTask task;
      task.grid_id = assigned_center_grid_ids[i];
      task.hull = state.center_hulls_[i];
      hull_tasks.push_back(std::move(task));
    }
  }
}

void C2ExplorationManager::keepCenterSubset(const vector<int>& keep_indices, const int drone_num, Eigen::MatrixXd& mat,
    vector<int>& center_grid_ids, vector<Vector3d>& center_positions,
    vector<vector<Vector3d>>& center_hulls, vector<int>& center_ids, vector<int>& center_types) {
  const int filtered_center_num = static_cast<int>(keep_indices.size());
  Eigen::MatrixXd filtered_mat =
      Eigen::MatrixXd::Zero(1 + drone_num + filtered_center_num, 1 + drone_num + filtered_center_num);
  for (int i = 0; i < 1 + drone_num; ++i) {
    for (int j = 0; j < 1 + drone_num; ++j) {
      filtered_mat(i, j) = mat(i, j);
    }
  }
  for (int i = 0; i < filtered_center_num; ++i) {
    const int src_i = 1 + drone_num + keep_indices[i];
    for (int j = 0; j < 1 + drone_num; ++j) {
      filtered_mat(j, 1 + drone_num + i) = mat(j, src_i);
      filtered_mat(1 + drone_num + i, j) = mat(src_i, j);
    }
    for (int j = 0; j < filtered_center_num; ++j) {
      const int src_j = 1 + drone_num + keep_indices[j];
      filtered_mat(1 + drone_num + i, 1 + drone_num + j) = mat(src_i, src_j);
    }
  }

  vector<int> filtered_center_grid_ids;
  vector<Vector3d> filtered_center_positions;
  vector<vector<Vector3d>> filtered_center_hulls;
  vector<int> filtered_center_ids;
  vector<int> filtered_center_types;
  filtered_center_grid_ids.reserve(filtered_center_num);
  filtered_center_positions.reserve(filtered_center_num);
  filtered_center_hulls.reserve(filtered_center_num);
  filtered_center_ids.reserve(filtered_center_num);
  filtered_center_types.reserve(filtered_center_num);
  for (const int idx : keep_indices) {
    filtered_center_grid_ids.push_back(center_grid_ids[idx]);
    filtered_center_positions.push_back(center_positions[idx]);
    filtered_center_hulls.push_back(center_hulls[idx]);
    filtered_center_ids.push_back(center_ids[idx]);
    filtered_center_types.push_back(center_types[idx]);
  }

  mat.swap(filtered_mat);
  center_grid_ids.swap(filtered_center_grid_ids);
  center_positions.swap(filtered_center_positions);
  center_hulls.swap(filtered_center_hulls);
  center_ids.swap(filtered_center_ids);
  center_types.swap(filtered_center_types);
}

}  // namespace c2_expl
