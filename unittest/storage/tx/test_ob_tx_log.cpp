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

#include <gtest/gtest.h>
#define private public
#include "storage/tx/ob_tx_log.h"
#include "logservice/ob_log_base_header.h"

namespace oceanbase
{
using namespace common;
using namespace transaction;
using namespace storage;
using namespace share;

namespace unittest
{
class TestObTxLog : public ::testing::Test
{
public:
  virtual void SetUp() {}
  virtual void TearDown() {}
public:
};

//const TEST
TxID TEST_TX_ID = 1024;
int64_t TEST_CLUSTER_VERSION = 1;
int64_t TEST_LOG_NO = 1;
ObAddr TEST_ADDR(ObAddr::VER::IPV4,"1.0.0.1",606);
int TEST_TRANS_TYPE = 1;
int TEST_SESSION_ID = 56831;
common::ObString TEST_TRACE_ID_STR("trace_id_test");
bool TEST_CAN_ELR =  false;
int64_t TEST_QUERY_TIME = 90000;
bool TEST_IS_DUP = true;
bool TEST_IS_SUB2PC = false;
int64_t TEST_EPOCH = 1315;
int64_t TEST_LAST_OP_SN = 1315;
int64_t TEST_FIRST_SCN = 1315;
int64_t TEST_LAST_SCN = 1315;
uint64_t TEST_ORG_CLUSTER_ID = 1208;
common::ObString TEST_TRCE_INFO("trace_info_test");
LogOffSet TEST_LOG_OFFSET(10);
int64_t TEST_COMMIT_VERSION = 190878;
int64_t TEST_CHECKSUM = 29890209;
int64_t TEST_SCHEMA_VERSION = 372837;
int64_t TEST_TX_EXPIRED_TIME = 12099087;
int64_t TEST_LOG_ENTRY_NO = 1233;
int64_t TEST_MAX_SUBMITTED_SEQ_NO = 12345;
LSKey TEST_LS_KEY;
ObXATransID TEST_XID;


struct OldTestLog
{
  ObTxSerCompatByte compat_bytes_;
  int64_t tx_id_1;
  int64_t tx_id_2;

  OldTestLog()
  {
    tx_id_1 = 0;
    tx_id_2 = 0;
    int ret = OB_SUCCESS;
    if (OB_FAIL(compat_bytes_.init(2))) {
      TRANS_LOG(WARN, "init compat_bytes_ failed", K(ret));
    }
  }
  OB_UNIS_VERSION(1);
};

OB_TX_SERIALIZE_MEMBER(OldTestLog, compat_bytes_, tx_id_1, tx_id_2);

struct NewTestLog
{
  ObTxSerCompatByte compat_bytes_;
  int64_t tx_id_1;
  int64_t tx_id_2;
  int64_t tx_id_3;

  NewTestLog()
  {
    tx_id_1 = 0;
    tx_id_2 = 0;
    tx_id_3 = 0;
    int ret = OB_SUCCESS;
    if (OB_FAIL(compat_bytes_.init(2))) {
      TRANS_LOG(WARN, "init compat_bytes_ failed", K(ret));
    }
  }
  OB_UNIS_VERSION(1);
};

OB_TX_SERIALIZE_MEMBER(NewTestLog, compat_bytes_, tx_id_1, tx_id_2, tx_id_3);

// test ObTxLogBlockHeader
TEST_F(TestObTxLog, tx_log_block_header)
{
  TRANS_LOG(INFO, "called", "func", test_info_->name());
  TxID id = 0;
  int64_t pos = 0;
  ObTxLogBlock fill_block, replay_block;

  ObTxLogBlockHeader fill_block_header(TEST_ORG_CLUSTER_ID, TEST_LOG_ENTRY_NO, ObTransID(TEST_TX_ID));
  ASSERT_EQ(OB_SUCCESS, fill_block.init(TEST_TX_ID, fill_block_header));

  // check log_block_header
  char *buf = fill_block.get_buf();
  logservice::ObLogBaseHeader base_header_1;
  logservice::ObLogBaseHeader base_header_2;

  pos = 0;
  base_header_1.deserialize(buf, base_header_1.get_serialize_size(),  pos);
  EXPECT_EQ(base_header_1.get_log_type() , ObTxLogBlock::DEFAULT_LOG_BLOCK_TYPE);
  EXPECT_EQ(base_header_1.get_replay_hint(), TEST_TX_ID);

  ObTxLogBlockHeader replay_block_header;
  ASSERT_EQ(OB_SUCCESS,
            replay_block.init_with_header(buf,
                                          fill_block.get_size(),
                                          id,
                                          replay_block_header));
  uint64_t tmp_cluster_id = replay_block_header.get_org_cluster_id();
  EXPECT_EQ(TEST_ORG_CLUSTER_ID, tmp_cluster_id);
  EXPECT_EQ(id, TEST_TX_ID);

  fill_block.reuse(id, replay_block_header);
  buf = fill_block.get_buf();
  pos = 0;
  base_header_2.deserialize(buf, base_header_2.get_serialize_size(),  pos);
  EXPECT_EQ(base_header_2.get_log_type() , ObTxLogBlock::DEFAULT_LOG_BLOCK_TYPE);
  EXPECT_EQ(base_header_2.get_replay_hint(), TEST_TX_ID);
}

TEST_F(TestObTxLog, tx_log_body_except_redo)
{
  TRANS_LOG(INFO, "called", "func", test_info_->name());
  ObTxLogBlock fill_block;
  ObTxLogBlock replay_block;

  ObLSArray TEST_LS_ARRAY;
  TEST_LS_ARRAY.push_back(LSKey());
  ObRedoLSNArray TEST_LOG_OFFSET_ARRY;
  TEST_LOG_OFFSET_ARRY.push_back(TEST_LOG_OFFSET);
  ObLSLogInfoArray TEST_INFO_ARRAY;
  TEST_INFO_ARRAY.push_back(ObLSLogInfo());
  ObTxBufferNodeArray TEST_TX_BUFFER_NODE_ARRAY;
  ObString str("TEST CASE");
  ObTxBufferNode node;
  node.init(ObTxDataSourceType::LS_TABLE, str);
  TEST_TX_BUFFER_NODE_ARRAY.push_back(node);

  ObTxCommitInfoLog fill_commit_state(TEST_ADDR,
                                       TEST_LS_ARRAY,
                                       TEST_LS_KEY,
                                       TEST_IS_SUB2PC,
                                       TEST_IS_DUP,
                                       TEST_CAN_ELR,
                                       TEST_TRACE_ID_STR,
                                       TEST_TRCE_INFO,
                                       TEST_LOG_OFFSET,
                                       TEST_LOG_OFFSET_ARRY,
                                       TEST_LS_ARRAY,
                                       TEST_CLUSTER_VERSION,
                                       TEST_XID);
  // ASSERT_EQ(OB_SUCCESS, fill_commit_state.before_serialize());
  ObTxActiveInfoLog fill_active_state(TEST_ADDR,
                                       TEST_TRANS_TYPE,
                                       TEST_SESSION_ID,
                                       TEST_TRACE_ID_STR,
                                       TEST_SCHEMA_VERSION,
                                       TEST_CAN_ELR,
                                       TEST_ADDR,
                                       TEST_QUERY_TIME,
                                       TEST_IS_SUB2PC,
                                       TEST_IS_DUP,
                                       TEST_TX_EXPIRED_TIME,
                                       TEST_EPOCH,
                                       TEST_LAST_OP_SN,
                                       TEST_FIRST_SCN,
                                       TEST_LAST_SCN,
                                       TEST_MAX_SUBMITTED_SEQ_NO,
                                       TEST_CLUSTER_VERSION);
  ObTxPrepareLog filll_prepare(TEST_LS_ARRAY, TEST_LOG_OFFSET);
  ObTxCommitLog fill_commit(share::SCN::base_scn(),
                            TEST_CHECKSUM,
                            TEST_LS_ARRAY,
                            TEST_TX_BUFFER_NODE_ARRAY,
                            TEST_TRANS_TYPE,
                            TEST_LOG_OFFSET,
                            TEST_INFO_ARRAY);
  ObTxClearLog fill_clear(TEST_LS_ARRAY);
  ObTxAbortLog fill_abort(TEST_TX_BUFFER_NODE_ARRAY);
  ObTxRecordLog fill_record(TEST_LOG_OFFSET, TEST_LOG_OFFSET_ARRY);

  ObTxLogBlockHeader header(TEST_ORG_CLUSTER_ID, TEST_LOG_ENTRY_NO, ObTransID(TEST_TX_ID));
  ASSERT_EQ(OB_SUCCESS, fill_block.init(TEST_TX_ID, header));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_active_state));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_commit_state));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(filll_prepare));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_commit));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_clear));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_abort));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_record));

  TxID id = 0;
  ObTxLogHeader tx_log_header;
  ObTxLogBlockHeader block_header;
  ASSERT_EQ(OB_SUCCESS, replay_block.init_with_header(fill_block.get_buf(), fill_block.get_size(), id, block_header));

  uint64_t tmp_cluster_id = block_header.get_org_cluster_id();
  EXPECT_EQ(TEST_ORG_CLUSTER_ID, tmp_cluster_id);

  ObTxActiveInfoLogTempRef active_temp_ref;
  ObTxActiveInfoLog replay_active_state(active_temp_ref);
  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(tx_log_header));
  EXPECT_EQ(ObTxLogType::TX_ACTIVE_INFO_LOG, tx_log_header.get_tx_log_type());
  ASSERT_EQ(OB_SUCCESS, replay_block.deserialize_log_body(replay_active_state));
  EXPECT_EQ(TEST_ADDR, replay_active_state.get_scheduler());
  EXPECT_EQ(TEST_TRANS_TYPE, replay_active_state.get_trans_type());
  EXPECT_EQ(TEST_SESSION_ID, replay_active_state.get_session_id());
  EXPECT_EQ(TEST_TRACE_ID_STR, replay_active_state.get_app_trace_id());
  EXPECT_EQ(TEST_SCHEMA_VERSION, replay_active_state.get_schema_version());
  EXPECT_EQ(TEST_CAN_ELR, replay_active_state.is_elr());
  EXPECT_EQ(TEST_ADDR, replay_active_state.get_proposal_leader());
  EXPECT_EQ(TEST_QUERY_TIME, replay_active_state.get_cur_query_start_time());
  EXPECT_EQ(TEST_IS_SUB2PC, replay_active_state.is_sub2pc());
  EXPECT_EQ(TEST_IS_DUP, replay_active_state.is_dup_tx());
  EXPECT_EQ(TEST_TX_EXPIRED_TIME, replay_active_state.get_tx_expired_time());
  EXPECT_EQ(TEST_EPOCH, replay_active_state.get_epoch());
  EXPECT_EQ(TEST_LAST_OP_SN, replay_active_state.get_last_op_sn());
  EXPECT_EQ(TEST_FIRST_SCN, replay_active_state.get_first_scn());
  EXPECT_EQ(TEST_LAST_SCN, replay_active_state.get_last_scn());
  EXPECT_EQ(TEST_CLUSTER_VERSION, replay_active_state.get_cluster_version());

  ObTxCommitInfoLogTempRef commit_state_temp_ref;
  ObTxCommitInfoLog replay_commit_state(commit_state_temp_ref);
  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(tx_log_header));
  EXPECT_EQ(ObTxLogType::TX_COMMIT_INFO_LOG, tx_log_header.get_tx_log_type());
  ASSERT_EQ(OB_INVALID_ARGUMENT,
            replay_block.deserialize_log_body(replay_active_state)); // error log type
  ASSERT_EQ(OB_SUCCESS, replay_block.deserialize_log_body(replay_commit_state));

  ObTxPrepareLogTempRef prepare_temp_ref;
  ObTxPrepareLog replay_prepare(prepare_temp_ref);
  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(tx_log_header));
  EXPECT_EQ(ObTxLogType::TX_PREPARE_LOG, tx_log_header.get_tx_log_type());
  ASSERT_EQ(OB_SUCCESS, replay_block.deserialize_log_body(replay_prepare));

  ObTxCommitLogTempRef commit_temp_ref;
  ObTxCommitLog replay_commit(commit_temp_ref);
  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(tx_log_header));
  EXPECT_EQ(ObTxLogType::TX_COMMIT_LOG, tx_log_header.get_tx_log_type());
  ASSERT_EQ(OB_SUCCESS, replay_block.deserialize_log_body(replay_commit));

  ObTxClearLogTempRef clear_temp_ref;
  ObTxClearLog replay_clear(clear_temp_ref);
  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(tx_log_header));
  EXPECT_EQ(ObTxLogType::TX_CLEAR_LOG, tx_log_header.get_tx_log_type());
  ASSERT_EQ(OB_SUCCESS, replay_block.deserialize_log_body(replay_clear));

  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(tx_log_header));
  EXPECT_EQ(ObTxLogType::TX_ABORT_LOG, tx_log_header.get_tx_log_type());

  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(tx_log_header));
  EXPECT_EQ(ObTxLogType::TX_RECORD_LOG, tx_log_header.get_tx_log_type());

  ASSERT_EQ(OB_ITER_END, replay_block.get_next_log(tx_log_header)); // ITER_END
}

TEST_F(TestObTxLog, tx_log_body_redo)
{
  TRANS_LOG(INFO, "called", "func", test_info_->name());
  ObTxLogBlock fill_block;
  ObTxLogBlock replay_block;
  ObTxLogBlock replay_block_2;

  ObLSArray TEST_LS_ARRAY;
  TEST_LS_ARRAY.push_back(LSKey());
  ObRedoLSNArray TEST_LOG_OFFSET_ARRY;
  TEST_LOG_OFFSET_ARRY.push_back(TEST_LOG_OFFSET);
  ObLSLogInfoArray TEST_INFO_ARRAY;
  TEST_INFO_ARRAY.push_back(ObLSLogInfo());
  ObTxBufferNodeArray TEST_TX_BUFFER_NODE_ARRAY;
  ObString str("TEST CASE");
  ObTxBufferNode node;
  node.init(ObTxDataSourceType::LS_TABLE, str);
  TEST_TX_BUFFER_NODE_ARRAY.push_back(node);

  ObTxCommitInfoLog fill_commit_state(TEST_ADDR,
                                       TEST_LS_ARRAY,
                                       TEST_LS_KEY,
                                       TEST_IS_SUB2PC,
                                       TEST_IS_DUP,
                                       TEST_CAN_ELR,
                                       TEST_TRACE_ID_STR,
                                       TEST_TRCE_INFO,
                                       TEST_LOG_OFFSET,
                                       TEST_LOG_OFFSET_ARRY,
                                       TEST_LS_ARRAY,
                                       TEST_CLUSTER_VERSION,
                                       TEST_XID);
  ObTxCommitLog fill_commit(share::SCN::base_scn(),
                            TEST_CHECKSUM,
                            TEST_LS_ARRAY,
                            TEST_TX_BUFFER_NODE_ARRAY,
                            TEST_TRANS_TYPE,
                            TEST_LOG_OFFSET,
                            TEST_INFO_ARRAY);

  ObTxLogBlockHeader fill_block_header(TEST_ORG_CLUSTER_ID, TEST_LOG_ENTRY_NO, ObTransID(TEST_TX_ID));
  ASSERT_EQ(OB_SUCCESS, fill_block.init(TEST_TX_ID, fill_block_header));

  ObString TEST_MUTATOR_BUF("FFF");
  int64_t mutator_pos = 0;
  ObCLogEncryptInfo TEST_CLOG_ENCRYPT_INFO;
  TEST_CLOG_ENCRYPT_INFO.init();
  ObTxRedoLog fill_redo(TEST_CLOG_ENCRYPT_INFO, TEST_LOG_NO, TEST_CLUSTER_VERSION);
  ASSERT_EQ(OB_SUCCESS, fill_block.prepare_mutator_buf(fill_redo));
  ASSERT_EQ(OB_SUCCESS,
            serialization::encode(fill_redo.get_mutator_buf(),
                                  fill_redo.get_mutator_size(),
                                  mutator_pos,
                                  TEST_MUTATOR_BUF));
  ASSERT_EQ(OB_SUCCESS, fill_block.finish_mutator_buf(fill_redo, mutator_pos));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_commit_state));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_commit));

  mutator_pos = 0;
  TxID id = 0;
  ObTxLogHeader log_header;
  ObString replay_mutator_buf;
  ObTxRedoLogTempRef redo_temp_ref;
  ObTxRedoLog replay_redo(redo_temp_ref);

  ObTxLogBlockHeader replay_block_header;
  ASSERT_EQ(OB_SUCCESS, replay_block.init_with_header(fill_block.get_buf(), fill_block.get_size(), id, replay_block_header));

  uint64_t tmp_cluster_id = replay_block_header.get_org_cluster_id();
  EXPECT_EQ(TEST_ORG_CLUSTER_ID, tmp_cluster_id);

  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(log_header));
  EXPECT_EQ(ObTxLogType::TX_REDO_LOG, log_header.get_tx_log_type());
  ASSERT_EQ(OB_SUCCESS, replay_block.deserialize_log_body(replay_redo));
  EXPECT_EQ(fill_redo.get_mutator_size(), replay_redo.get_mutator_size());
  TRANS_LOG(INFO,
            "Mutator Info",
            K(fill_redo.get_mutator_buf()),
            K(replay_redo.get_replay_mutator_buf()),
            K(replay_redo.get_mutator_size()));
  ASSERT_EQ(OB_SUCCESS,
            serialization::decode(replay_redo.get_replay_mutator_buf(),
                                  replay_redo.get_mutator_size(),
                                  mutator_pos,
                                  replay_mutator_buf));
  EXPECT_EQ(TEST_MUTATOR_BUF, replay_mutator_buf);
  // EXPECT_EQ(TEST_CLOG_ENCRYPT_INFO,replay_redo.get_clog_encrypt_info());
  // EXPECT_EQ(TEST_LOG_NO,replay_redo.get_log_no());
  EXPECT_EQ(TEST_CLUSTER_VERSION,replay_redo.get_cluster_version());
  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(log_header));
  EXPECT_EQ(ObTxLogType::TX_COMMIT_INFO_LOG, log_header.get_tx_log_type());
  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(log_header));
  EXPECT_EQ(ObTxLogType::TX_COMMIT_LOG, log_header.get_tx_log_type());


  //ignore replay log, only need commit log
  ObTxLogBlockHeader replay_block_header_2;
  ASSERT_EQ(OB_SUCCESS, replay_block_2.init_with_header(fill_block.get_buf(), fill_block.get_size(), id, replay_block_header_2));

  tmp_cluster_id = replay_block_header_2.get_org_cluster_id();
  EXPECT_EQ(TEST_ORG_CLUSTER_ID, tmp_cluster_id);

  ASSERT_EQ(OB_SUCCESS, replay_block_2.get_next_log(log_header));
  EXPECT_EQ(ObTxLogType::TX_REDO_LOG, log_header.get_tx_log_type());
  // ASSERT_EQ(OB_SUCCESS, replay_block.deserialize_log_body(replay_redo));
  // ASSERT_EQ(OB_SUCCESS,
  //           serialization::decode(replay_redo.get_replay_mutator_buf(),
  //                                 replay_redo.get_mutator_size(),
  //                                 mutator_pos,
  //                                 replay_mutator_buf));
  // EXPECT_EQ(TEST_MUTATOR_BUF, replay_mutator_buf);
  // EXPECT_EQ(TEST_CLOG_ENCRYPT_INFO,replay_redo.get_clog_encrypt_info());
  // EXPECT_EQ(TEST_LOG_NO,replay_redo.get_log_no());
  // EXPECT_EQ(TEST_CLUSTER_VERSION,replay_redo.get_cluster_version());
  ASSERT_EQ(OB_SUCCESS, replay_block_2.get_next_log(log_header));
  EXPECT_EQ(ObTxLogType::TX_COMMIT_INFO_LOG, log_header.get_tx_log_type());
  ASSERT_EQ(OB_SUCCESS, replay_block_2.get_next_log(log_header));
  EXPECT_EQ(ObTxLogType::TX_COMMIT_LOG, log_header.get_tx_log_type());
  ObTxCommitLogTempRef commit_temp_ref;
  ObTxCommitLog replay_commit(commit_temp_ref);
  ASSERT_EQ(OB_SUCCESS, replay_block_2.deserialize_log_body(replay_commit));
  EXPECT_EQ(share::SCN::base_scn(), replay_commit.get_commit_version());

}

TEST_F(TestObTxLog, test_compat_bytes)
{
  ObLSArray TEST_LS_ARRAY;
  TEST_LS_ARRAY.push_back(LSKey());
  ObRedoLSNArray TEST_LOG_OFFSET_ARRY;
  TEST_LOG_OFFSET_ARRY.push_back(TEST_LOG_OFFSET);
  ObLSLogInfoArray TEST_INFO_ARRAY;
  TEST_INFO_ARRAY.push_back(ObLSLogInfo());
  ObTxBufferNodeArray TEST_TX_BUFFER_NODE_ARRAY;
  ObString str("TEST CASE");
  ObTxBufferNode node;
  node.init(ObTxDataSourceType::LS_TABLE, str);
  TEST_TX_BUFFER_NODE_ARRAY.push_back(node);

  ObTxCommitInfoLog fill_commit_info(TEST_ADDR,
                                     TEST_LS_ARRAY,
                                     TEST_LS_KEY,
                                     TEST_IS_SUB2PC,
                                     TEST_IS_DUP,
                                     TEST_CAN_ELR,
                                     TEST_TRACE_ID_STR,
                                     TEST_TRCE_INFO,
                                     TEST_LOG_OFFSET,
                                     TEST_LOG_OFFSET_ARRY,
                                     TEST_LS_ARRAY,
                                     TEST_CLUSTER_VERSION,
                                     TEST_XID);
  ObTxCommitInfoLogTempRef commit_info_temp_ref;
  ObTxCommitInfoLog replay_commit_info(commit_info_temp_ref);

  // ASSERT_EQ(OB_SUCCESS, fill_commit_info.before_serialize());
  TRANS_LOG(INFO,
            "the size of commit info with all compat_bytes",
            K(fill_commit_info.get_serialize_size()));
  ASSERT_EQ(true, fill_commit_info.is_dup_tx());
  ASSERT_EQ(false, fill_commit_info.get_participants().empty());
  void *tmp_buf = ob_malloc(1 * 1024 * 1024);
  int64_t pos = 0;
  fill_commit_info.compat_bytes_.set_object_flag(2, false);
  fill_commit_info.compat_bytes_.set_object_flag(5, false);
  ASSERT_EQ(OB_SUCCESS, fill_commit_info.serialize((char *)tmp_buf, 1 * 1024 * 1024, pos));
  TRANS_LOG(INFO,
            "the size of commit info with compat_bytes",
            K(fill_commit_info.get_serialize_size()),
            K(pos));

  pos = 0;
  ASSERT_EQ(OB_SUCCESS,
            replay_commit_info.deserialize((const char *)tmp_buf, 1 * 1024 * 1024, pos));
  EXPECT_EQ(replay_commit_info.is_dup_tx(), false);
  EXPECT_EQ(replay_commit_info.get_participants().empty(), true);

}

TEST_F(TestObTxLog, test_default_log_deserialize)
{
  ObTxLogBlock fill_block;
  ObTxLogBlock replay_block;

  ObTxCommitInfoLogTempRef fill_commit_state_ref;
  ObTxActiveInfoLogTempRef fill_active_state_ref;
  ObTxPrepareLogTempRef fill_prepare_ref;
  ObTxCommitLogTempRef fill_commit_ref;
  ObTxClearLogTempRef fill_clear_ref;
  ObTxAbortLogTempRef fill_abort_ref;
  ObTxRecordLogTempRef fill_record_ref;

  ObTxCommitInfoLog fill_commit_state(fill_commit_state_ref);
  ObTxActiveInfoLog fill_active_state(fill_active_state_ref);
  ObTxPrepareLog fill_prepare(fill_prepare_ref);
  ObTxCommitLog fill_commit(fill_commit_ref);
  ObTxClearLog fill_clear(fill_clear_ref);
  ObTxAbortLog fill_abort(fill_abort_ref);
  ObTxRecordLog fill_record(fill_record_ref);

  // ObTxLogBlockHeader fill_block_header(TEST_ORG_CLUSTER_ID, TEST_LOG_ENTRY_NO,
  // ObTransID(TEST_TX_ID));
  ObTxLogBlockHeader fill_block_header;
  ASSERT_EQ(OB_SUCCESS, fill_block.init(TEST_TX_ID, fill_block_header));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_active_state));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_commit_state));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_prepare));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_commit));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_clear));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_abort));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_record));

  TxID id = 0;
  int64_t fill_member_cnt = 0;
  int64_t replay_member_cnt = 0;
  ObTxLogHeader tx_log_header;
  ObTxLogBlockHeader replay_block_header;
  ASSERT_EQ(OB_SUCCESS, replay_block.init_with_header(fill_block.get_buf(), fill_block.get_size(),
                                                      id, replay_block_header));
  fill_member_cnt = fill_block_header.compat_bytes_.total_obj_cnt_;
  EXPECT_EQ(fill_block_header.get_org_cluster_id(), replay_block_header.get_org_cluster_id());
  replay_member_cnt++;
  EXPECT_EQ(fill_block_header.get_log_entry_no(), replay_block_header.get_log_entry_no());
  replay_member_cnt++;
  EXPECT_EQ(fill_block_header.get_tx_id().get_id(), replay_block_header.get_tx_id().get_id());
  replay_member_cnt++;
  EXPECT_EQ(replay_member_cnt, fill_member_cnt);

  ObTxActiveInfoLogTempRef active_temp_ref;
  ObTxActiveInfoLog replay_active_state(active_temp_ref);
  fill_member_cnt = replay_member_cnt = 0;
  fill_member_cnt = fill_active_state.compat_bytes_.total_obj_cnt_;
  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(tx_log_header));
  EXPECT_EQ(ObTxLogType::TX_ACTIVE_INFO_LOG, tx_log_header.get_tx_log_type());
  ASSERT_EQ(OB_SUCCESS, replay_block.deserialize_log_body(replay_active_state));
  EXPECT_EQ(fill_active_state.get_scheduler(), replay_active_state.get_scheduler());
  replay_member_cnt++;
  EXPECT_EQ(fill_active_state.get_trans_type(), replay_active_state.get_trans_type());
  replay_member_cnt++;
  EXPECT_EQ(fill_active_state.get_session_id(), replay_active_state.get_session_id());
  replay_member_cnt++;
  EXPECT_EQ(fill_active_state.get_app_trace_id(), replay_active_state.get_app_trace_id());
  replay_member_cnt++;
  EXPECT_EQ(fill_active_state.get_schema_version(), replay_active_state.get_schema_version());
  replay_member_cnt++;
  EXPECT_EQ(fill_active_state.is_elr(), replay_active_state.is_elr());
  replay_member_cnt++;
  EXPECT_EQ(fill_active_state.get_proposal_leader(), replay_active_state.get_proposal_leader());
  replay_member_cnt++;
  EXPECT_EQ(fill_active_state.get_cur_query_start_time(),
            replay_active_state.get_cur_query_start_time());
  replay_member_cnt++;
  EXPECT_EQ(fill_active_state.is_sub2pc(), replay_active_state.is_sub2pc());
  replay_member_cnt++;
  EXPECT_EQ(fill_active_state.is_dup_tx(), replay_active_state.is_dup_tx());
  replay_member_cnt++;
  EXPECT_EQ(fill_active_state.get_tx_expired_time(), replay_active_state.get_tx_expired_time());
  replay_member_cnt++;
  EXPECT_EQ(fill_active_state.get_epoch(), replay_active_state.get_epoch());
  replay_member_cnt++;
  EXPECT_EQ(fill_active_state.get_last_op_sn(), replay_active_state.get_last_op_sn());
  replay_member_cnt++;
  EXPECT_EQ(fill_active_state.get_first_scn(), replay_active_state.get_first_scn());
  replay_member_cnt++;
  EXPECT_EQ(fill_active_state.get_last_scn(), replay_active_state.get_last_scn());
  replay_member_cnt++;
  EXPECT_EQ(fill_active_state.get_cluster_version(), replay_active_state.get_cluster_version());
  replay_member_cnt++;
  EXPECT_EQ(fill_active_state.get_max_submitted_seq_no(), replay_active_state.get_max_submitted_seq_no());
  replay_member_cnt++;
  EXPECT_EQ(replay_member_cnt, fill_member_cnt);

  ObTxCommitInfoLogTempRef commit_state_temp_ref;
  ObTxCommitInfoLog replay_commit_state(commit_state_temp_ref);
  fill_member_cnt = replay_member_cnt = 0;
  fill_member_cnt = fill_commit_state.compat_bytes_.total_obj_cnt_;
  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(tx_log_header));
  EXPECT_EQ(ObTxLogType::TX_COMMIT_INFO_LOG, tx_log_header.get_tx_log_type());
  ASSERT_EQ(OB_SUCCESS, replay_block.deserialize_log_body(replay_commit_state));
  EXPECT_EQ(fill_commit_state.get_scheduler(), replay_commit_state.get_scheduler());
  replay_member_cnt++;
  EXPECT_EQ(fill_commit_state.get_participants().count(),
            replay_commit_state.get_participants().count());
  replay_member_cnt++;
  EXPECT_EQ(fill_commit_state.get_upstream(), replay_commit_state.get_upstream());
  replay_member_cnt++;
  EXPECT_EQ(fill_commit_state.is_sub2pc(), replay_commit_state.is_sub2pc());
  replay_member_cnt++;
  EXPECT_EQ(fill_commit_state.is_dup_tx(), replay_commit_state.is_dup_tx());
  replay_member_cnt++;
  EXPECT_EQ(fill_commit_state.is_elr(), replay_commit_state.is_elr());
  replay_member_cnt++;
  EXPECT_EQ(fill_commit_state.get_incremental_participants().count(),
            replay_commit_state.get_incremental_participants().count());
  replay_member_cnt++;
  EXPECT_EQ(fill_commit_state.get_cluster_version(), replay_commit_state.get_cluster_version());
  replay_member_cnt++;
  EXPECT_EQ(fill_commit_state.get_app_trace_id(), replay_commit_state.get_app_trace_id());
  replay_member_cnt++;
  EXPECT_EQ(fill_commit_state.get_app_trace_info(), replay_commit_state.get_app_trace_info());
  replay_member_cnt++;
  EXPECT_EQ(fill_commit_state.get_prev_record_lsn(), replay_commit_state.get_prev_record_lsn());
  replay_member_cnt++;
  EXPECT_EQ(fill_commit_state.get_redo_lsns().count(), replay_commit_state.get_redo_lsns().count());
  replay_member_cnt++;
  EXPECT_EQ(fill_commit_state.get_xid(), replay_commit_state.get_xid());
  replay_member_cnt++;
  EXPECT_EQ(replay_member_cnt, fill_member_cnt);

  ObTxPrepareLogTempRef prepare_temp_ref;
  ObTxPrepareLog replay_prepare(prepare_temp_ref);
  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(tx_log_header));
  EXPECT_EQ(ObTxLogType::TX_PREPARE_LOG, tx_log_header.get_tx_log_type());
  ASSERT_EQ(OB_SUCCESS, replay_block.deserialize_log_body(replay_prepare));
  fill_member_cnt = replay_member_cnt = 0;
  fill_member_cnt = fill_prepare.compat_bytes_.total_obj_cnt_;
  EXPECT_EQ(fill_prepare.get_incremental_participants().count(),
            replay_prepare.get_incremental_participants().count());
  replay_member_cnt++;
  EXPECT_EQ(fill_prepare.get_prev_lsn(), replay_prepare.get_prev_lsn());
  replay_member_cnt++;
  EXPECT_EQ(replay_member_cnt, fill_member_cnt);

  ObTxCommitLogTempRef commit_temp_ref;
  ObTxCommitLog replay_commit(commit_temp_ref);
  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(tx_log_header));
  EXPECT_EQ(ObTxLogType::TX_COMMIT_LOG, tx_log_header.get_tx_log_type());
  ASSERT_EQ(OB_SUCCESS, replay_block.deserialize_log_body(replay_commit));

  ObTxClearLogTempRef clear_temp_ref;
  ObTxClearLog replay_clear(clear_temp_ref);
  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(tx_log_header));
  EXPECT_EQ(ObTxLogType::TX_CLEAR_LOG, tx_log_header.get_tx_log_type());
  ASSERT_EQ(OB_SUCCESS, replay_block.deserialize_log_body(replay_clear));

  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(tx_log_header));
  EXPECT_EQ(ObTxLogType::TX_ABORT_LOG, tx_log_header.get_tx_log_type());

  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(tx_log_header));
  EXPECT_EQ(ObTxLogType::TX_RECORD_LOG, tx_log_header.get_tx_log_type());

  ASSERT_EQ(OB_ITER_END, replay_block.get_next_log(tx_log_header)); // ITER_END
}

TEST_F(TestObTxLog, TestComapt)
{
  OldTestLog old_fill;
  NewTestLog new_fill;
  old_fill.tx_id_1 = TEST_TX_ID;
  old_fill.tx_id_2 = TEST_TX_ID;
  new_fill.tx_id_1 = TEST_TX_ID;
  new_fill.tx_id_2 = TEST_TX_ID;
  new_fill.tx_id_3 = TEST_TX_ID;

  char old_fill_buf[1024];
  char new_fill_buf[1024];
  memset(old_fill_buf, 0, sizeof(char) * 1024);
  memset(new_fill_buf, 0, sizeof(char) * 1024);

  int64_t old_pos,new_pos;
  int64_t old_size,new_size;

  old_pos = new_pos = 0;
  EXPECT_EQ(OB_SUCCESS, old_fill.serialize(old_fill_buf, 1024, old_pos));
  EXPECT_EQ(OB_SUCCESS, new_fill.serialize(new_fill_buf, 1024, new_pos));
  old_size = old_pos;
  new_size = new_pos;
  TRANS_LOG(INFO, " serialize test fill log", K(old_size), K(new_size));

  OldTestLog old_replay;
  NewTestLog new_replay;
  old_pos = new_pos = 0;
  EXPECT_EQ(OB_SUCCESS, new_replay.deserialize(old_fill_buf, old_size, old_pos));
  EXPECT_EQ(OB_SUCCESS, old_replay.deserialize(new_fill_buf, new_size, new_pos));

  EXPECT_EQ(TEST_TX_ID, old_replay.tx_id_1);
  EXPECT_EQ(TEST_TX_ID, old_replay.tx_id_2);

  EXPECT_EQ(TEST_TX_ID, new_replay.tx_id_1);
  EXPECT_EQ(TEST_TX_ID, new_replay.tx_id_2);
  EXPECT_EQ(0, new_replay.tx_id_3);

}

} // namespace unittest

} // namespace oceanbase

using namespace oceanbase;

int main(int argc, char **argv)
{
  int ret = 1;
  ObLogger &logger = ObLogger::get_logger();
  logger.set_file_name("test_ob_tx_log.log", true);
  logger.set_log_level(OB_LOG_LEVEL_INFO);
  testing::InitGoogleTest(&argc, argv);
  ret = RUN_ALL_TESTS();
  return ret;
}
