#ifndef _C2_EXPLORATION_MANAGER_H_
#define _C2_EXPLORATION_MANAGER_H_

#include <ros/ros.h>
#include <Eigen/Eigen>
#include <memory>
#include <vector>
#include <limits>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include <active_perception/hgrid.h>
#include <exploration_manager/PeerReachabilityQuery.h>
#include <exploration_manager/PeerReachabilityResult.h>
#include <exploration_manager/PeerTakeoverGoal.h>
#include <exploration_manager/PeerTakeoverReceipt.h>
#include <exploration_manager/ReachEvidence.h>
#include <map>

using Eigen::Vector3d;
using std::shared_ptr;
using std::vector;

namespace c2_expl {
class EDTEnvironment;
class SDFMap;
class CommunicationGraph;
class FastPlannerManager;
// class UniformGrid;
class FrontierFinder;
struct ExplorationParam;
struct ExplorationData;
struct DroneState;

enum EXPL_RESULT { NO_FRONTIER, FAIL, SUCCEED, NO_GRID, HANDOFF };

class C2ExplorationManager {
public:
  C2ExplorationManager();
  ~C2ExplorationManager();

  void initialize(ros::NodeHandle& nh);

  // Read-only experiment telemetry. It is disabled unless ECRTA_TELEMETRY_DIR is set.
  void logTelemetryEvent(const std::string& event, const std::string& fields = "") const;
  // Unified method mode: baseline | suppress | reach | svr | steer.
  std::string methodMode() const { return method_mode_; }
  bool methodSuppressionActive() const;
  bool methodReachActive() const { return method_mode_ == "reach"; }
  bool methodSvrActive() const { return method_mode_ == "svr"; }
  bool methodSteerActive() const { return method_mode_ == "steer"; }
  bool methodSuppressActive() const { return method_mode_ == "suppress"; }
  double methodRiskWeight() const { return reach_risk_weight_; }
  double methodRiskPenalty() const { return reach_risk_penalty_; }
  double methodGoalMinHoldS() const { return steer_goal_min_hold_s_; }
  double methodSwitchMargin() const { return steer_switch_margin_; }
  double methodLoadBias() const { return steer_load_bias_; }
  int methodRepeatThreshold() const;
  double methodCooldownS() const;
  double methodExecutionRisk(const int frontier_id, const Vector3d& goal) const;
  int repeatCountForTarget(const int frontier_id, const Vector3d& goal) const;
  double methodSteerCandidateScore(
      const Vector3d& reference, const int frontier_id, const Vector3d& goal) const;
  void logTelemetryFile(const std::string& filename, const std::string& line) const;
  void logFailureEvent(
      const int frontier_id, const Vector3d& goal, const uint64_t map_version,
      const int repeat_count, const std::string& reason) const;
  void logCommandEvent(const std::string& event, const std::string& fields) const;
  void logMethodEvent(const std::string& event, const std::string& fields) const;
  void logTaskLedgerEvent(const std::string& event, const std::string& fields) const;

  // Sequence of the most recently accepted local trajectory plan. This is telemetry-only
  // metadata used to pair execution lifecycle events with their planned trajectory.
  std::uint64_t activeTelemetryPlanSeq() const { return telemetry_active_plan_seq_; }
  std::uint64_t activeTelemetryAllocationSeq() const {
    return telemetry_active_allocation_seq_;
  }

  // Read-only task identity fields for experiment auditing. The returned JSON fragment
  // deliberately reports an unmatched target instead of inferring a task identity.
  std::string telemetryTaskFields(const Vector3d& target_pos) const;

  // Copy generated LKH artifacts into the run telemetry directory before they are overwritten
  // by the next allocation. This is audit data only and never changes solver inputs.
  void snapshotTelemetryFile(const std::string& source_path, const std::string& snapshot_name) const;

  // Active peer handoff control surface. These methods do not change maps, task
  // definitions, trajectory generation, or the original C2 allocation cost model.
  bool hasPendingPeerHandoff();
  double peerHandoffWaitRemainingS() const;
  void clearPeerHandoff();
  bool isPrctTargetCooled(const int frontier_id, const Vector3d& goal) const;
  bool shouldSuppressPrctTarget(const int frontier_id, const Vector3d& goal) const;
  std::vector<int> prctFilterCooledTargets(
      const std::vector<int>& frontier_ids,
      const Vector3d& reference) const;
  void registerPrctFailure(const int frontier_id, const Vector3d& goal);
  void registerPrctSuccess(const int frontier_id, const Vector3d& goal);
  double prctFailureCooldownS(const int repeat_count) const;
  std::string prctCooldownKey(
      const int frontier_id, const Vector3d& goal, const uint64_t map_version) const;
  uint64_t currentMapVersion() const;
  uint64_t prctGoalEvidenceHash(const Vector3d& goal) const;
  bool prctGoalPresentInCurrentViewpoints(const Vector3d& goal) const;
  bool takeNextPeerTakeoverGoal(Vector3d& goal, double& yaw, int& frontier_id,
      std::uint64_t& owner_plan_seq, std::uint8_t& owner_drone_id,
      std::uint64_t& request_id, double& issued_wall_s);

  int planExploreMotion(
      const Vector3d& pos, const Vector3d& vel, const Vector3d& acc, const Vector3d& yaw);

  int planTrajToView(const Vector3d& pos, const Vector3d& vel, const Vector3d& acc,
      const Vector3d& yaw, const Vector3d& next_pos, const double& next_yaw);

  int updateFrontierStruct(const Eigen::Vector3d& pos);

  struct AllocationRequest {
    vector<Eigen::Vector3d> drone_positions;
    vector<Eigen::Vector3d> drone_velocities;
    vector<int> drone_ids;
    vector<int> grid_ids;
    const vector<DroneState>* drone_states = nullptr;
    // Non-hull centers are protected by connectivity graph node id.
    vector<int> blocked_center_node_ids;
    // Split hull tasks are protected by hull geometry.
    vector<vector<Vector3d>> blocked_center_hulls;
  };

  struct AllocationResult {
    vector<vector<Vector3d>> centers;
    vector<vector<vector<Vector3d>>> center_hulls;
  };

  void allocateTasks(const AllocationRequest& request, AllocationResult& result);

  // Find optimal tour visiting unknown grid
  bool findGlobalTourOfGrid(const vector<Eigen::Vector3d>& positions,
      const vector<Eigen::Vector3d>& velocities, vector<int>& ids, vector<vector<int>>& others,
      bool init = false);

  shared_ptr<ExplorationData> ed_;
  shared_ptr<ExplorationParam> ep_;
  shared_ptr<CommunicationGraph> comm_graph_;
  shared_ptr<FastPlannerManager> planner_manager_;
  shared_ptr<FrontierFinder> frontier_finder_;
  shared_ptr<HGrid> hgrid_;
  shared_ptr<SDFMap> sdf_map_;
  // shared_ptr<UniformGrid> uniform_grid_;

private:
  static constexpr int kLkhCostScale = 100;
  static constexpr int kLkhPrecision = 1;
  static constexpr int kLkhMaxEdgeWeight =
      std::numeric_limits<int>::max() / 2 / kLkhPrecision - 1;
  static constexpr double kLkhFallbackBlockedCost = 1000.0;

  struct AllocationCandidateSet {
    Eigen::MatrixXd mat;
    vector<int> center_grid_ids;
    vector<Vector3d> center_positions;
    vector<vector<Vector3d>> center_hulls;
    vector<int> center_ids;
    vector<int> center_types;
    int full_center_num = 0;
  };

  // Find optimal coordinated tour for quadrotor swarm
  void findGridAndFrontierPath(const Vector3d& cur_pos, const Vector3d& cur_vel,
      const Vector3d& cur_yaw, vector<int>& grid_ids, vector<int>& ftr_ids);

  void shortenPath(vector<Vector3d>& path);

  void findTourOfFrontier(const Vector3d& cur_pos, const Vector3d& cur_vel, const Vector3d& cur_yaw,
      const vector<int>& ftr_ids, const vector<Eigen::Vector3d>& grid_pos, vector<int>& ids);

  bool buildAllocationCandidateSet(
      const AllocationRequest& request, AllocationCandidateSet& candidates);

  // Diagnostic-only probe invoked after an ordinary A* failure. It never changes
  // the selected target or C2 state.
  void runReachabilityShadowProbe(
      const Vector3d& start_pos, const std::uint64_t plan_seq, const int failed_frontier_id);

  // Diagnostic-only cross-UAV probe. It tests the failed target from positions available in
  // this process's latest swarm state; it never changes ownership or planning state.
  void runPeerReachabilityShadowProbe(
      const Vector3d& failed_start, const Vector3d& failed_goal,
      const std::uint64_t plan_seq, const int failed_frontier_id,
      const double failed_goal_yaw);

  // Read-only probe executed by the target peer on its own node and map.
  void runPeerLocalMapReachabilityShadowProbe(
      const Vector3d& failed_goal, const std::uint64_t plan_seq,
      const int failed_frontier_id, const double failed_goal_yaw);
  void peerReachabilityQueryCallback(
      const exploration_manager::PeerReachabilityQueryConstPtr& msg);
  void peerReachabilityResultCallback(
      const exploration_manager::PeerReachabilityResultConstPtr& msg);
  void peerTakeoverGoalCallback(
      const exploration_manager::PeerTakeoverGoalConstPtr& msg);
  void publishPeerTakeoverGoal(const int peer_id, const std::uint64_t plan_seq,
      const int frontier_id, const Vector3d& goal, const double goal_yaw);
  void peerTakeoverReceiptCallback(
      const exploration_manager::PeerTakeoverReceiptConstPtr& msg);
  void sendPeerTakeoverReceipt(const std::uint8_t owner_drone_id,
      const std::uint64_t request_id, const std::uint64_t owner_plan_seq,
      const int frontier_id, const Vector3d& goal, const double goal_yaw,
      const double issued_wall_s, const std::uint8_t status,
      const std::string& status_name, const std::string& reason);
  bool isPrctTakeoverCooled(const int frontier_id, const Vector3d& goal) const;
  bool isC3TakeoverCompleted(const int frontier_id) const;
  int prctTakeoverAttemptCount(const int frontier_id, const Vector3d& goal) const;
  void registerPrctTakeover(
      const int frontier_id, const Vector3d& goal,
      const bool long_cooldown = false);
  int prctFailureRepeatCount(const int frontier_id, const Vector3d& goal) const;
  int selectBestPeerCertificate() const;
  double estimateOwnerStuckCostS() const;
  double peerTrust(const int peer_id) const;
  void updatePeerTrust(const int peer_id, const std::uint8_t status);

  static int toLkhEdgeWeight(const double raw_cost, int& sanitized_count);
  static void parseMultiTours(
      const vector<int>& ids, const int drone_num, const int dimension, vector<vector<int>>& tours);
  static bool pointInsideConvexHull2D(
      const vector<Vector3d>& hull, const Vector3d& p, const double tol = 0.25);
  static Vector3d hullCentroid(const vector<Vector3d>& hull);
  static bool hasExplicitSplitCenter(
      const vector<int>& candidate_indices, const vector<vector<Vector3d>>& center_hulls);
  static bool candidateBlockedByHull(
      const Vector3d& center, const vector<vector<Vector3d>>& blocked_center_hulls);
  static void filterMeetingOptCentersByParticipantTasks(const shared_ptr<HGrid>& hgrid,
      const vector<DroneState>& participant_states, const vector<int>& center_grid_ids,
      const vector<Vector3d>& center_positions, const vector<int>& center_ids,
      const vector<int>& center_types, const vector<vector<Vector3d>>& center_hulls,
      vector<int>& selected_indices);
  static void collectAssignedHullTasks(const shared_ptr<HGrid>& hgrid,
      const vector<DroneState>* states, vector<HGrid::HullTask>& hull_tasks);
  static void keepCenterSubset(const vector<int>& keep_indices, const int drone_num,
      Eigen::MatrixXd& mat, vector<int>& center_grid_ids, vector<Vector3d>& center_positions,
      vector<vector<Vector3d>>& center_hulls, vector<int>& center_ids, vector<int>& center_types);

  shared_ptr<EDTEnvironment> edt_environment_;
  ros::ServiceClient tsp_client_, acvrp_client_;
  std::string telemetry_path_;
  std::string telemetry_dir_;
  // Monotonic local identifier for read-only allocation telemetry. It never
  // participates in C2 planning or task costs.
  std::uint64_t telemetry_allocation_seq_ = 0;
  std::uint64_t telemetry_active_allocation_seq_ = 0;
  std::uint64_t telemetry_plan_seq_ = 0;
  std::uint64_t telemetry_active_plan_seq_ = 0;
  std::uint64_t telemetry_lkh_snapshot_seq_ = 0;
  std::int64_t telemetry_active_frontier_id_ = -1;
  std::vector<int> telemetry_active_frontier_candidates_;
  std::vector<Vector3d> telemetry_active_local_view_candidates_;
  int reachability_shadow_max_candidates_ = 0;
  int reachability_peer_shadow_max_peers_ = 0;
  bool prct_enable_retry_suppression_ = false;
  // B1+ switch: when true, a confirmed failure target is quarantined until its
  // local occupancy evidence changes, the goal disappears, or A* succeeds.
  bool prct_backoff_enabled_ = false;
  int prct_repeat_threshold_ = 3;
  double prct_cooldown_s_ = 5.0;
  double prct_backoff_initial_s_ = 5.0;
  double prct_backoff_max_s_ = 30.0;
  double prct_backoff_factor_ = 2.0;
  double prct_local_evidence_radius_m_ = 0.2;
  bool prct_evict_on_first_failure_ = false;
  double prct_eviction_max_extra_cost_ = 20.0;
  // Coarse task-map epoch. It advances only when the frontier viewpoint set changes,
  // so a transient per-frame point cloud version cannot defeat retry suppression.
  std::uint64_t prct_map_epoch_ = 0;
  std::uint64_t prct_epoch_last_hash_ = 0;

  struct PrctCooldownEntry {
    int repeat_count = 0;
    double cooldown_until_wall_s = 0.0;
    uint64_t map_version = 0;
    Eigen::Vector3d goal = Eigen::Vector3d::Zero();
    uint64_t goal_evidence_hash = 0;
  };
  std::unordered_map<std::string, PrctCooldownEntry> prct_cooldowns_;
  std::unordered_map<int, PrctCooldownEntry> prct_frontier_cooldowns_;

  // Three-method unified control surface (METHOD1 REACH, METHOD2 SVR, METHOD3 STEER).
  std::string method_mode_ = "baseline";
  bool method_telemetry_enabled_ = false;
  double reach_risk_weight_ = 0.25;
  double reach_risk_penalty_ = 1.0;
  double steer_goal_min_hold_s_ = 3.0;
  double steer_switch_margin_ = 0.2;
  double steer_load_bias_ = 0.1;
  double svr_reallocation_cost_m_ = 2.0;
  double svr_solver_cost_s_ = 0.5;
  double reach_center_match_radius_m_ = 5.0;
  // REACH v2: cross-drone failure-evidence board.
  struct ReachEvidenceEntry {
    int count = 0;
    double last_wall_s = 0.0;
    Vector3d goal = Vector3d::Zero();
  };
  std::map<std::string, ReachEvidenceEntry> reach_evidence_board_;
  double reach_evidence_window_s_ = 30.0;
  double reach_evidence_publish_interval_s_ = 1.0;
  double reach_cooldown_s_ = 30.0;
  double reach_center_evidence_radius_m_ = 5.0;
  ros::Publisher reach_evidence_pub_;
  ros::Subscriber reach_evidence_sub_;
  ros::Timer reach_evidence_timer_;
  std::string reachEvidenceKey(const Vector3d& goal) const;
  void reachEvidencePublishCallback(const ros::TimerEvent&);
  void reachEvidenceReceiveCallback(const exploration_manager::ReachEvidenceConstPtr& msg);
  void reachEvidenceBoardPrune(const double now_wall_s);
  double methodCenterRisk(const Vector3d& center) const;
  int lkh_seed_ = 1;
  std::string svr_last_allocation_digest_;
  struct SvrCandidateSnapshot {
    int count = 0;
    int blocked_node_count = 0;
    int blocked_hull_count = 0;
    std::vector<int> grid_ids;
    std::vector<int> center_ids;
    std::vector<int> center_types;
    std::vector<int> hull_sizes;
    std::vector<Vector3d> positions;
  };
  SvrCandidateSnapshot svr_last_candidate_snapshot_;
  std::vector<std::vector<Vector3d>> svr_last_centers_;
  std::vector<std::vector<std::vector<Vector3d>>> svr_last_center_hulls_;
  int svr_last_drone_num_ = 0;
  double svr_reuse_match_radius_m_ = 5.0;
  std::string failures_telemetry_path_;
  std::string task_ledger_telemetry_path_;
  std::string command_telemetry_path_;
  std::string method_telemetry_path_;
  std::unordered_map<std::string, uint64_t> task_ledger_seq_;
  std::unordered_map<int, double> task_goal_set_wall_s_;
  std::unordered_map<std::string, double> task_goal_set_by_coord_wall_s_;
  std::unordered_map<int, int> task_repeat_count_;
  ros::Publisher peer_reachability_query_pub_;
  ros::Subscriber peer_reachability_query_sub_;
  ros::Publisher peer_reachability_result_pub_;
  ros::Subscriber peer_reachability_result_sub_;
  ros::Publisher peer_takeover_goal_pub_;
  ros::Subscriber peer_takeover_goal_sub_;
  std::uint64_t peer_reachability_request_seq_ = 0;

  struct PendingPeerTakeoverGoal {
    std::uint64_t owner_plan_seq = 0;
    std::uint8_t owner_drone_id = 0;
    std::uint64_t request_id = 0;
    double issued_wall_s = 0.0;
    int frontier_id = -1;
    Vector3d goal = Vector3d::Zero();
    double goal_yaw = 0.0;
    double received_wall_s = 0.0;
  };
  std::vector<PendingPeerTakeoverGoal> pending_peer_takeover_goals_;
  int peer_takeover_goal_max_queue_ = 3;
  double peer_takeover_goal_timeout_s_ = 8.0;

  bool peer_handoff_pending_ = false;
  std::uint64_t peer_handoff_plan_seq_ = 0;
  int peer_handoff_frontier_id_ = -1;
  Vector3d peer_handoff_goal_ = Vector3d::Zero();
  double peer_handoff_goal_yaw_ = 0.0;
  double peer_handoff_started_wall_s_ = 0.0;
  double peer_handoff_wait_s_ = 8.0;
  bool peer_handoff_published_ = false;
  bool peer_handoff_observed_ = false;
  int peer_handoff_best_peer_id_ = -1;
  int peer_handoff_publish_count_ = 0;

  enum PeerTakeoverStatus : std::uint8_t {
    TAKEOVER_ACCEPTED = 0,
    TAKEOVER_REJECTED = 1,
    TAKEOVER_COMPLETED = 2,
    TAKEOVER_ABORTED = 3,
    TAKEOVER_STALE = 4,
  };

  bool prct_enable_peer_takeover_ = false;
  double prct_peer_cert_wait_s_ = 0.25;
  double prct_peer_handoff_timeout_s_ = 2.0;
  double prct_peer_state_max_age_s_ = 2.0;
  double peer_handoff_deadline_wall_s_ = 0.0;
  double peer_handoff_cert_window_end_wall_s_ = 0.0;
  std::string peer_handoff_receipt_status_;
  std::string peer_handoff_fallback_reason_;
  std::uint64_t peer_handoff_request_id_ = 0;
  std::uint64_t peer_handoff_map_version_ = 0;
  std::unordered_map<std::string, PrctCooldownEntry> prct_takeover_cooldowns_;

  struct PeerTrustEntry {
    double alpha = 1.0;
    double beta = 1.0;
    double trust = 0.5;
    std::uint64_t success_count = 0;
    std::uint64_t reject_count = 0;
    std::uint64_t abort_count = 0;
    std::uint64_t stale_count = 0;
  };
  std::unordered_map<int, PeerTrustEntry> peer_trust_;

  struct PendingPeerCertificate {
    int peer_id = -1;
    double path_length_m = 0.0;
    double peer_state_age_s = 0.0;
    double peer_load = 0.0;
    double duration_wall_s = 0.0;
    std::uint64_t request_id = 0;
    double peer_trust = 0.5;
    double peer_marginal_cost_s = 0.0;
    double expected_benefit_s = 0.0;
  };
  double computePeerMarginalCostS(const PendingPeerCertificate& cert) const;
  std::vector<PendingPeerCertificate> pending_peer_certificates_;
  ros::Publisher peer_takeover_receipt_pub_;
  ros::Subscriber peer_takeover_receipt_sub_;
  std::uint64_t peer_takeover_request_seq_ = 0;
  bool c3_enable_marginal_gate_ = false;
  double c3_benefit_margin_s_ = 1.0;
  double c3_trust_threshold_ = 0.5;
  double c3_load_weight_ = 0.5;
  double c3_handoff_overhead_s_ = 0.5;
  double c3_trust_penalty_s_ = 2.0;
  double c3_nominal_speed_m_s_ = 2.0;
  double c3_owner_fallback_penalty_s_ = 3.0;
  double c3_owner_stuck_alpha_ = 1.0;
  int c3_min_repeat_count_ = 3;
  double c3_owner_repeat_cost_s_ = 0.3;
  double c3_peer_cert_grace_s_ = 0.6;
  double c3_takeover_cooldown_s_ = 30.0;
  int c3_max_takeover_attempts_ = 3;
  double c3_takeover_completed_cooldown_s_ = 120.0;
  std::unordered_map<int, double> c3_takeover_completed_until_wall_s_;
 std::unordered_map<std::string, int> c3_failure_repeat_counts_;
  std::unordered_map<std::string, int> baseline_failure_repeat_counts_;
  std::unordered_map<std::string, double> c3_failure_chain_start_wall_s_;
  bool c3MarginalGateActive() const {
    return prct_enable_peer_takeover_ && c3_enable_marginal_gate_;
  }
  double peer_handoff_owner_stuck_cost_s_ = 0.0;
  double peer_handoff_selected_benefit_s_ = 0.0;
  double peer_handoff_selected_cost_s_ = 0.0;

public:
  typedef shared_ptr<C2ExplorationManager> Ptr;
};

}  // namespace c2_expl

#endif
