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

#ifndef OCEANBASE_TRANSACTION_OB_CTX_TX_DATA_
#define OCEANBASE_TRANSACTION_OB_CTX_TX_DATA_

#include "lib/lock/ob_spin_rwlock.h"
#include "storage/tx/ob_tx_data_define.h"

namespace oceanbase
{

namespace storage
{
class ObTxTable;
}

namespace transaction
{

class ObCtxTxData
{
public:
  ObCtxTxData() { reset(); }
  void reset();
  void destroy();

  int init(ObLSTxCtxMgr *ctx_mgr, int64_t tx_id);

  bool is_read_only() const { return read_only_; }
  int insert_into_tx_table();
  int recover_tx_data(const storage::ObTxData *tmp_tx_data);
  int replace_tx_data(storage::ObTxData *&tmp_tx_data);
  int deep_copy_tx_data_out(storage::ObTxData *&tmp_tx_data);
  int alloc_tmp_tx_data(storage::ObTxData *&tmp_tx_data);
  int free_tmp_tx_data(storage::ObTxData *&tmp_tx_data);
  int insert_tmp_tx_data(storage::ObTxData *&tmp_tx_data);

  void get_tx_table(storage::ObTxTable *&tx_table);

  int set_state(int32_t state);
  int set_commit_version(const share::SCN &commit_version);
  int set_start_log_ts(const share::SCN &start_ts);
  int set_end_log_ts(const share::SCN &end_ts);

  int32_t get_state() const;
  const share::SCN get_commit_version() const;
  const share::SCN get_start_log_ts() const;
  const share::SCN get_end_log_ts() const;

  ObTransID get_tx_id() const;

  int prepare_add_undo_action(ObUndoAction &undo_action,
                              storage::ObTxData *&tmp_tx_data,
                              storage::ObUndoStatusNode *&tmp_undo_status);
  int cancel_add_undo_action(storage::ObTxData *tmp_tx_data, storage::ObUndoStatusNode *tmp_undo_status);
  int commit_add_undo_action(ObUndoAction &undo_action, storage::ObUndoStatusNode &tmp_undo_status);
  int add_undo_action(ObUndoAction &undo_action, storage::ObUndoStatusNode *tmp_undo_status = NULL);

  int get_tx_commit_data(const storage::ObTxCommitData *&tx_commit_data) const;

  TO_STRING_KV(KP(ctx_mgr_), KPC(tx_data_), K(tx_commit_data_), K(read_only_));
public:
  class Guard { // TODO(yunxing.cyx): remove it
    friend class ObCtxTxData;
    Guard(ObCtxTxData &host) : host_(host) { }
    ObCtxTxData &host_;
  public:
    ~Guard() { }
    int get_tx_data(const storage::ObTxData *&tx_data) const;
  };
  Guard get_tx_data() { return Guard(*this); }
public:
  //only for unittest
  void test_init(storage::ObTxData &tx_data, ObLSTxCtxMgr *ctx_mgr)
  {
    ctx_mgr_ = ctx_mgr;
    tx_data_ = &tx_data;
    read_only_ = false;
  }
  void test_tx_data_reset()
  {
    if (OB_NOT_NULL(tx_data_)) {
      tx_data_->reset();
    }
  }
  void test_set_tx_id(int64_t tx_id)
  {
    if (OB_NOT_NULL(tx_data_)) {
      tx_data_->tx_id_ = tx_id;
    }
  }

private:
  int check_tx_data_writable_();
  int insert_tx_data_(storage::ObTxTable *tx_table, storage::ObTxData *&tx_data);
  int free_tx_data_(storage::ObTxTable *tx_table, storage::ObTxData *&tx_data);
  int deep_copy_tx_data_(storage::ObTxTable *tx_table, storage::ObTxData *&tx_data);

private:
  typedef common::SpinRWLock RWLock;
  typedef common::SpinRLockGuard RLockGuard;
  typedef common::SpinWLockGuard WLockGuard;
private:
  ObLSTxCtxMgr *ctx_mgr_;
  storage::ObTxData *tx_data_;
  storage::ObTxCommitData tx_commit_data_;
  bool read_only_;
  // lock for tx_data_ pointer
  RWLock lock_;
};

} // namespace transaction
} // namespace oceanbase

#endif
