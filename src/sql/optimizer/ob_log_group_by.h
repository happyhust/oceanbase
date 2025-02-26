/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef OCEANBASE_SQL_OB_LOG_GROUP_BY_H
#define OCEANBASE_SQL_OB_LOG_GROUP_BY_H
#include "lib/allocator/page_arena.h"
#include "ob_logical_operator.h"
#include "ob_select_log_plan.h"
#include "sql/engine/aggregate/ob_adaptive_bypass_ctrl.h"
namespace oceanbase
{
using namespace common;
namespace sql
{
class ObLogSort;
struct ObThreeStageAggrInfo
{
  ObThreeStageAggrInfo() : distinct_aggr_count_(-1),
    aggr_code_idx_(-1),
    aggr_code_expr_(NULL),
    distinct_aggr_batch_(),
    distinct_exprs_()
  {}

  int64_t distinct_aggr_count_;
  int64_t aggr_code_idx_;
  ObRawExpr *aggr_code_expr_;
  ObArray<ObDistinctAggrBatch, ModulePageAllocator, true> distinct_aggr_batch_;
  common::ObArray<ObRawExpr *, common::ModulePageAllocator, true> distinct_exprs_;

  int assign(const ObThreeStageAggrInfo &info);
};

struct ObRollupAdaptiveInfo
{
  ObRollupAdaptiveInfo()
  : rollup_id_expr_(NULL),
    rollup_status_(ObRollupStatus::NONE_ROLLUP),
    sort_keys_(),
    enable_encode_sort_(false)
  {}

  ObRawExpr *rollup_id_expr_;
  ObRollupStatus rollup_status_;
  ObArray<OrderItem, common::ModulePageAllocator, true> sort_keys_;
  bool enable_encode_sort_;

  int assign(const ObRollupAdaptiveInfo &info);
};

class ObLogGroupBy : public ObLogicalOperator
{
public:
  ObLogGroupBy(ObLogPlan &plan)
      : ObLogicalOperator(plan),
        group_exprs_(),
        rollup_exprs_(),
        aggr_exprs_(),
        algo_(AGGREGATE_UNINITIALIZED),
        distinct_card_(0.0),
        distinct_per_dop_(0.0),
        from_pivot_(false),
        is_push_down_(false),
        is_partition_gi_(false),
        total_ndv_(-1.0),
        origin_child_card_(-1.0),
        aggr_stage_(ObThreeStageAggrStage::NONE_STAGE),
        three_stage_info_(),
        rollup_adaptive_info_(),
        force_push_down_(false)
  {}
  virtual ~ObLogGroupBy()
  {}

  //const char* get_name() const;
  virtual int32_t get_explain_name_length() const;
  virtual int get_explain_name_internal(char *buf,
                                        const int64_t buf_len,
                                        int64_t &pos);
  int set_first_stage_info(ObRawExpr *aggr_code_expr,
                           ObIArray<ObDistinctAggrBatch> &batch);

  int set_second_stage_info(ObRawExpr *aggr_code_expr,
                            ObIArray<ObDistinctAggrBatch> &batch,
                            ObIArray<ObRawExpr *> &distinct_exprs);

  int set_third_stage_info(ObRawExpr *aggr_code_expr,
                           ObIArray<ObDistinctAggrBatch> &batch);

  int set_rollup_info(const ObRollupStatus rollup_status,
                               ObRawExpr *rollup_id_expr);
  int set_rollup_info(const ObRollupStatus rollup_status,
                      ObRawExpr *rollup_id_expr,
                      ObIArray<OrderItem> &sort_keys,
                      bool enable_encode_sort);

  const ObIArray<ObDistinctAggrBatch> &get_distinct_aggr_batch()
  { return three_stage_info_.distinct_aggr_batch_; }


  // Get the 'group-by' expressions
  inline common::ObIArray<ObRawExpr *> &get_group_by_exprs()
  { return group_exprs_; }
  // Get the 'rollup' expressions
  inline common::ObIArray<ObRawExpr *> &get_rollup_exprs()
  { return rollup_exprs_; }
  // Get the aggregate expressions
  inline common::ObIArray<ObRawExpr *> &get_aggr_funcs()
  { return aggr_exprs_; }
  inline bool has_rollup()
  { return rollup_exprs_.count() > 0; }
  inline void set_hash_type() { algo_ = HASH_AGGREGATE; }
  inline void set_merge_type() { algo_ = MERGE_AGGREGATE; }
  inline void set_scalar_type() { algo_ = SCALAR_AGGREGATE; }
  inline void set_algo_type(AggregateAlgo type) { algo_ = type; }
  inline AggregateAlgo get_algo() const { return algo_; }

  // @brief SET the GROUP-BY COLUMNS
  int set_group_by_exprs(const common::ObIArray<ObRawExpr *> &group_by_exprs);
  // @brief SET the ROLLUP COLUMNS
  int set_rollup_exprs(const common::ObIArray<ObRawExpr *> &rollup_exprs);
  int set_aggr_exprs(const common::ObIArray<ObAggFunRawExpr *> &aggr_exprs);
  ObSelectLogPlan *get_plan() { return static_cast<ObSelectLogPlan *>(my_plan_); }
  virtual int get_op_exprs(ObIArray<ObRawExpr*> &all_exprs) override;
  virtual uint64_t hash(uint64_t seed) const override;
  virtual int est_cost() override;
  virtual int est_width() override;
  virtual int re_est_cost(EstimateCostInfo &param, double &card, double &cost) override;
  int inner_est_cost(double child_card,
                     double &child_ndv,
                     double &per_dop_ndv,
                     double &op_cost);
  int get_child_est_info(double &child_card, double &child_ndv, double &selectivity);
  int get_gby_output_exprs(ObIArray<ObRawExpr *> &output_exprs);
  virtual bool is_block_op() const override
  { return (MERGE_AGGREGATE != get_algo() && !is_adaptive_aggregate())
        || ObRollupStatus::ROLLUP_DISTRIBUTOR == rollup_adaptive_info_.rollup_status_; }
  virtual int generate_link_sql_post(GenLinkStmtPostContext &link_ctx) override;

  virtual int compute_fd_item_set() override;
  virtual int compute_op_ordering() override;
  double get_distinct_card() const { return distinct_card_; }
  void set_distinct_card(const double distinct_card) { distinct_card_ = distinct_card; }
  bool from_pivot() const { return from_pivot_; }
  void set_from_pivot(const bool value) { from_pivot_ = value; }
  int get_group_rollup_exprs(common::ObIArray<ObRawExpr *> &group_rollup_exprs) const;
  inline bool is_push_down() const { return is_push_down_; }
  inline void set_push_down(const bool is_push_down) { is_push_down_ = is_push_down; }
  inline void set_partition_gi(bool is_partition_gi) { is_partition_gi_ = is_partition_gi; }
  inline bool is_partition_gi() { return is_partition_gi_; }
  inline double get_total_ndv() const { return total_ndv_; }
  inline void set_total_ndv(double total_ndv) { total_ndv_ = total_ndv; }
  inline double get_origin_child_card() const { return origin_child_card_; }
  inline void set_origin_child_card(double card) { origin_child_card_ = card; }
  inline bool force_partition_gi() const { return (is_partition_wise() && !is_push_down()) || (is_partition_gi_); }


  int allocate_startup_expr_post()override;

  inline ObThreeStageAggrStage get_aggr_stage() const { return aggr_stage_; }
  inline int64_t get_aggr_code_idx() const { return three_stage_info_.aggr_code_idx_; }
  inline ObRawExpr* get_aggr_code_expr() { return three_stage_info_.aggr_code_expr_; }
  inline int64_t get_distinct_aggr_count() const { return three_stage_info_.distinct_aggr_count_; }
  inline common::ObIArray<ObRawExpr *> &get_distinct_exprs() { return three_stage_info_.distinct_exprs_; }

  inline bool is_three_stage_aggr() const { return ObThreeStageAggrStage::NONE_STAGE != aggr_stage_; }
  inline bool is_first_stage() const { return ObThreeStageAggrStage::FIRST_STAGE == aggr_stage_; }
  inline bool is_second_stage() const { return ObThreeStageAggrStage::SECOND_STAGE == aggr_stage_; }
  inline bool is_third_stage() const { return ObThreeStageAggrStage::THIRD_STAGE == aggr_stage_; }
  inline bool force_push_down() const { return force_push_down_; }
  inline bool is_adaptive_aggregate() const { return HASH_AGGREGATE == get_algo()
                                                     && !force_push_down()
                                                     && (is_first_stage() || (!is_three_stage_aggr() && is_push_down())); }


  inline void set_rollup_status(const ObRollupStatus rollup_status)
  { rollup_adaptive_info_.rollup_status_ = rollup_status; }
  inline ObRollupStatus get_rollup_status() const
  { return rollup_adaptive_info_.rollup_status_; }
  inline ObRawExpr *get_rollup_id_expr()
  { return rollup_adaptive_info_.rollup_id_expr_; }
  inline ObIArray<OrderItem> &get_inner_sort_keys()
  { return rollup_adaptive_info_.sort_keys_; }
  inline bool has_encode_sort()
  { return rollup_adaptive_info_.enable_encode_sort_; }
  inline bool is_rollup_distributor() const
  { return ObRollupStatus::ROLLUP_DISTRIBUTOR == rollup_adaptive_info_.rollup_status_; }
  inline bool is_rollup_collector() const
  { return ObRollupStatus::ROLLUP_COLLECTOR == rollup_adaptive_info_.rollup_status_; }
  inline void set_force_push_down(bool force_push_down)
  { force_push_down_ = force_push_down; }

  VIRTUAL_TO_STRING_KV(K_(group_exprs), K_(rollup_exprs), K_(aggr_exprs), K_(algo), K_(distinct_card),
      K_(is_push_down));
private:
  virtual int print_my_plan_annotation(char *buf,
                                       int64_t &buf_len,
                                       int64_t &pos,
                                       ExplainType type);
  virtual int inner_replace_generated_agg_expr(
      const common::ObIArray<std::pair<ObRawExpr *, ObRawExpr *> >&to_replace_exprs);
  virtual int allocate_granule_post(AllocGIContext &ctx) override;
  virtual int allocate_granule_pre(AllocGIContext &ctx) override;
  int create_fd_item_from_select_list(ObFdItemSet *fd_item_set);
  virtual int compute_one_row_info() override;
  virtual int print_outline(planText &plan);
  int print_used_hint(planText &plan_text);
  int print_outline_data(planText &plan_text);
private:
  common::ObSEArray<ObRawExpr *, 8, common::ModulePageAllocator, true> group_exprs_;
  common::ObSEArray<ObRawExpr *, 8, common::ModulePageAllocator, true> rollup_exprs_;
  common::ObSEArray<ObRawExpr *, 8, common::ModulePageAllocator, true> aggr_exprs_;
  AggregateAlgo algo_;
  // used for the execution engine to set hash bucket size
  double distinct_card_;
  double distinct_per_dop_;
  bool from_pivot_;
  bool is_push_down_;
  bool is_partition_gi_;
  double total_ndv_;
  double origin_child_card_;

  ObThreeStageAggrStage aggr_stage_;
  ObThreeStageAggrInfo three_stage_info_;
  // for rollup distributor and collector
  ObRollupAdaptiveInfo rollup_adaptive_info_;
  bool force_push_down_; // control by _aggregation_optimization_settings
};
} // end of namespace sql
} // end of namespace oceanbase

#endif // OCEANBASE_SQL_OB_LOG_GROUP_BY_H
