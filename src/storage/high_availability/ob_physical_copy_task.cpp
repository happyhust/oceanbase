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

#define USING_LOG_PREFIX STORAGE
#include "storage/high_availability/ob_physical_copy_task.h"
#include "storage/tablet/ob_tablet_create_delete_helper.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "observer/ob_server_event_history_table_operator.h"
#include "share/ob_cluster_version.h"
#include "share/rc/ob_tenant_base.h"

namespace oceanbase
{
using namespace share;
namespace storage
{

/******************ObPhysicalCopyCtx*********************/
ObPhysicalCopyCtx::ObPhysicalCopyCtx()
  : tenant_id_(OB_INVALID_ID),
    ls_id_(),
    tablet_id_(),
    src_info_(),
    bandwidth_throttle_(nullptr),
    svr_rpc_proxy_(nullptr),
    is_leader_restore_(false),
    restore_base_info_(nullptr),
    second_meta_index_store_(nullptr),
    ha_dag_(nullptr),
    sstable_index_builder_(nullptr),
    restore_macro_block_id_mgr_(nullptr),
    need_check_seq_(false),
    ls_rebuild_seq_(-1)
{
}

ObPhysicalCopyCtx::~ObPhysicalCopyCtx()
{
}

bool ObPhysicalCopyCtx::is_valid() const
{
  bool bool_ret = false;
  bool_ret = tenant_id_ != OB_INVALID_ID && ls_id_.is_valid() && tablet_id_.is_valid()
      && OB_NOT_NULL(bandwidth_throttle_) && OB_NOT_NULL(svr_rpc_proxy_) && OB_NOT_NULL(ha_dag_)
      && OB_NOT_NULL(sstable_index_builder_) && ((need_check_seq_ && ls_rebuild_seq_ >= 0) || !need_check_seq_);
  if (bool_ret) {
    if (!is_leader_restore_) {
      bool_ret = src_info_.is_valid();
    } else if (OB_ISNULL(restore_base_info_) || OB_ISNULL(second_meta_index_store_)
        || OB_ISNULL(restore_macro_block_id_mgr_)) {
      bool_ret = false;
    }
  }
  return bool_ret;
}

void ObPhysicalCopyCtx::reset()
{
  tenant_id_ = OB_INVALID_ID;
  ls_id_.reset();
  tablet_id_.reset();
  src_info_.reset();
  bandwidth_throttle_ = nullptr;
  svr_rpc_proxy_ = nullptr;
  is_leader_restore_ = false;
  restore_base_info_ = nullptr;
  meta_index_store_ = nullptr;
  second_meta_index_store_ = nullptr;
  ha_dag_ = nullptr;
  sstable_index_builder_ = nullptr;
  restore_macro_block_id_mgr_ = nullptr;
  need_check_seq_ = false;
  ls_rebuild_seq_ = -1;
}

/******************ObPhysicalCopyTaskInitParam*********************/
ObPhysicalCopyTaskInitParam::ObPhysicalCopyTaskInitParam()
  : tenant_id_(OB_INVALID_ID),
    ls_id_(),
    tablet_id_(),
    src_info_(),
    sstable_param_(nullptr),
    sstable_macro_range_info_(),
    tablet_copy_finish_task_(nullptr),
    ls_(nullptr),
    is_leader_restore_(false),
    restore_base_info_(nullptr),
    second_meta_index_store_(nullptr),
    need_check_seq_(false),
    ls_rebuild_seq_(-1)
{
}

ObPhysicalCopyTaskInitParam::~ObPhysicalCopyTaskInitParam()
{
}

bool ObPhysicalCopyTaskInitParam::is_valid() const
{
  bool bool_ret = false;
  bool_ret = tenant_id_ != OB_INVALID_ID && ls_id_.is_valid() && tablet_id_.is_valid() && OB_NOT_NULL(sstable_param_)
      && sstable_macro_range_info_.is_valid() && OB_NOT_NULL(tablet_copy_finish_task_) && OB_NOT_NULL(ls_)
      && ((need_check_seq_ && ls_rebuild_seq_ >= 0) || !need_check_seq_);
  if (bool_ret) {
    if (!is_leader_restore_) {
      bool_ret = src_info_.is_valid();
    } else if (OB_ISNULL(restore_base_info_)
        || OB_ISNULL(meta_index_store_)
        || OB_ISNULL(second_meta_index_store_)) {
      bool_ret = false;
    }
  }
  return bool_ret;
}

void ObPhysicalCopyTaskInitParam::reset()
{
  tenant_id_ = OB_INVALID_ID;
  ls_id_.reset();
  tablet_id_.reset();
  src_info_.reset();
  sstable_param_ = nullptr;
  sstable_macro_range_info_.reset();
  tablet_copy_finish_task_ = nullptr;
  ls_ = nullptr;
  is_leader_restore_ = false;
  restore_base_info_ = nullptr;
  meta_index_store_ = nullptr;
  second_meta_index_store_ = nullptr;
  need_check_seq_ = false;
  ls_rebuild_seq_ = -1;
}

/******************ObPhysicalCopyTask*********************/
ObPhysicalCopyTask::ObPhysicalCopyTask()
  : ObITask(TASK_TYPE_MIGRATE_COPY_PHYSICAL),
    is_inited_(false),
    copy_ctx_(nullptr),
    finish_task_(nullptr),
    copy_table_key_(),
    copy_macro_range_info_()
{
}

ObPhysicalCopyTask::~ObPhysicalCopyTask()
{
}

int ObPhysicalCopyTask::init(
    ObPhysicalCopyCtx *copy_ctx,
    ObPhysicalCopyFinishTask *finish_task)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("physical copy task init tiwce", K(ret));
  } else if (OB_ISNULL(copy_ctx) || !copy_ctx->is_valid() || OB_ISNULL(finish_task)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("physical copy task get invalid argument", K(ret), KPC(copy_ctx), KPC(finish_task));
  } else if (OB_FAIL(build_macro_block_copy_info_(finish_task))) {
    LOG_WARN("failed to build macro block copy info", K(ret), KPC(copy_ctx));
  } else {
    copy_ctx_ = copy_ctx;
    finish_task_ = finish_task;
    is_inited_ = true;
  }
  return ret;
}

int ObPhysicalCopyTask::build_macro_block_copy_info_(ObPhysicalCopyFinishTask *finish_task)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(finish_task)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("build macro block copy info get invalid argument", K(ret), KP(finish_task));
  } else if (OB_FAIL(finish_task->get_macro_block_copy_info(copy_table_key_, copy_macro_range_info_))) {
    if (OB_ITER_END == ret) {
    } else {
      LOG_WARN("failed to get macro block copy info", K(ret));
    }
  } else {
    LOG_INFO("succeed get macro block copy info", K(copy_table_key_), K(copy_macro_range_info_));
  }
  return ret;
}

int ObPhysicalCopyTask::process()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObMacroBlocksWriteCtx copied_ctx;
  int64_t copy_count = 0;
  int64_t reuse_count = 0;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("physical copy task do not init", K(ret));
  } else if (copy_ctx_->ha_dag_->get_ha_dag_net_ctx()->is_failed()) {
    FLOG_INFO("ha dag net is already failed, skip physical copy task", KPC(copy_ctx_));
  } else {
    if (copy_ctx_->tablet_id_.is_inner_tablet() || copy_ctx_->tablet_id_.is_ls_inner_tablet()) {
    } else {
      DEBUG_SYNC(FETCH_MACRO_BLOCK);
    }

    if (OB_SUCC(ret) && copy_macro_range_info_->macro_block_count_ > 0) {
      if (OB_FAIL(fetch_macro_block_with_retry_(copied_ctx))) {
        LOG_WARN("failed to fetch major block", K(ret), K(copy_table_key_), KPC(copy_macro_range_info_));
      } else if (copy_macro_range_info_->macro_block_count_ != copied_ctx.get_macro_block_count()) {
        ret = OB_ERR_SYS;
        LOG_ERROR("list count not match", K(ret), KPC(copy_macro_range_info_),
            K(copied_ctx.get_macro_block_count()), K(copied_ctx));
      }
    }
    LOG_INFO("physical copy task finish", K(ret), KPC(copy_macro_range_info_), KPC(copy_ctx_));
  }
  if (OB_SUCCESS != (tmp_ret = record_server_event_())) {
    LOG_WARN("failed to record server event", K(tmp_ret), K(ret));
  }

  if (OB_FAIL(ret)) {
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = ObStorageHADagUtils::deal_with_fo(ret, copy_ctx_->ha_dag_))) {
      LOG_WARN("failed to deal with fo", K(ret), K(tmp_ret), KPC(copy_ctx_));
    }
  }

  return ret;
}

int ObPhysicalCopyTask::fetch_macro_block_with_retry_(
    ObMacroBlocksWriteCtx &copied_ctx)
{
  int ret = OB_SUCCESS;
  int64_t retry_times = 0;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("physical copy task do not init", K(ret));
  } else {
    while (retry_times < MAX_RETRY_TIMES) {
      if (retry_times > 0) {
        LOG_INFO("retry get major block", K(retry_times));
      }
      if (OB_FAIL(fetch_macro_block_(retry_times, copied_ctx))) {
        STORAGE_LOG(WARN, "failed to fetch major block", K(ret), K(retry_times));
      }

      if (OB_SUCC(ret)) {
        break;
      }

      if (OB_FAIL(ret)) {
        copied_ctx.clear();
        retry_times++;
        ob_usleep(OB_FETCH_MAJOR_BLOCK_RETRY_INTERVAL);
      }
    }
  }

  return ret;
}

int ObPhysicalCopyTask::fetch_macro_block_(
    const int64_t retry_times,
    ObMacroBlocksWriteCtx &copied_ctx)
{
  int ret = OB_SUCCESS;
  ObStorageHAMacroBlockWriter *writer = NULL;
  ObICopyMacroBlockReader *reader = NULL;
  ObIndexBlockRebuilder index_block_rebuilder;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("physical copy physical task do not init", K(ret));
  } else {
    LOG_INFO("init reader", K(copy_table_key_));

    if (OB_FAIL(index_block_rebuilder.init(*copy_ctx_->sstable_index_builder_))) {
      LOG_WARN("failed to init index block rebuilder", K(ret), K(copy_table_key_));
    } else if (OB_FAIL(get_macro_block_reader_(reader))) {
      LOG_WARN("fail to get macro block reader", K(ret));
    } else if (OB_FAIL(get_macro_block_writer_(reader, &index_block_rebuilder, writer))) {
      LOG_WARN("failed to get macro block writer", K(ret), K(copy_table_key_));
    } else if (OB_FAIL(writer->process(copied_ctx))) {
      LOG_WARN("failed to process writer", K(ret), K(copy_table_key_));
    } else if (copy_macro_range_info_->macro_block_count_ != copied_ctx.get_macro_block_count()) {
      ret = OB_ERR_SYS;
      LOG_ERROR("list count not match", K(ret), K(copy_table_key_), KPC(copy_macro_range_info_),
          K(copied_ctx.get_macro_block_count()), K(copied_ctx));
    }

#ifdef ERRSIM
    if (OB_SUCC(ret)) {
      ret = E(EventTable::EN_MIGRATE_FETCH_MACRO_BLOCK) OB_SUCCESS;
      if (OB_FAIL(ret)) {
        if (retry_times == 0) {
        } else {
          ret = OB_SUCCESS;
        }
        STORAGE_LOG(ERROR, "fake EN_MIGRATE_FETCH_MACRO_BLOCK", K(ret));
      }
    }
#endif

    if (FAILEDx(index_block_rebuilder.close())) {
      LOG_WARN("failed to close index block builder", K(ret), K(copied_ctx));
    }

    if (NULL != reader) {
      free_macro_block_reader_(reader);
    }
    if (NULL != writer) {
      free_macro_block_writer_(writer);
    }
  }
  return ret;
}

int ObPhysicalCopyTask::get_macro_block_reader_(
    ObICopyMacroBlockReader *&reader)
{
  int ret = OB_SUCCESS;
  ObCopyMacroBlockReaderInitParam init_param;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("physical copy task do not init", K(ret));
  } else if (OB_FAIL(build_copy_macro_block_reader_init_param_(init_param))) {
    LOG_WARN("failed to build macro block reader init param", K(ret), KPC(copy_ctx_));
  } else if (copy_ctx_->is_leader_restore_) {
    if (OB_FAIL(get_macro_block_restore_reader_(init_param, reader))) {
      STORAGE_LOG(WARN, "failed to get_macro_block_restore_reader_", K(ret));
    }
  } else {
    if (OB_FAIL(get_macro_block_ob_reader_(init_param, reader))) {
      STORAGE_LOG(WARN, "failed to get_macro_block_ob_reader", K(ret));
    }
  }
  return ret;
}

int ObPhysicalCopyTask::get_macro_block_ob_reader_(
    const ObCopyMacroBlockReaderInitParam &init_param,
    ObICopyMacroBlockReader *&reader)
{
  int ret = OB_SUCCESS;
  ObCopyMacroBlockObReader *tmp_reader = NULL;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("physical copy task do not init", K(ret));
  } else if (copy_ctx_->is_leader_restore_ || !init_param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get macro block ob reader get invalid argument", K(ret), K(init_param));
  } else {
    void *buf = mtl_malloc(sizeof(ObCopyMacroBlockObReader), "MacroObReader");
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory", K(ret), KP(buf));
    } else if (FALSE_IT(tmp_reader = new(buf) ObCopyMacroBlockObReader())) {
    } else if (OB_FAIL(tmp_reader->init(init_param))) {
      STORAGE_LOG(WARN, "failed to init ob reader", K(ret));
    } else {
      reader = tmp_reader;
      tmp_reader = NULL;
    }

    if (OB_FAIL(ret)) {
      if (NULL != reader) {
        reader->~ObICopyMacroBlockReader();
        mtl_free(reader);
        reader = NULL;
      }
    }
    if (NULL != tmp_reader) {
      tmp_reader->~ObCopyMacroBlockObReader();
      mtl_free(tmp_reader);
      tmp_reader = NULL;
    }
  }
  return ret;
}

int ObPhysicalCopyTask::get_macro_block_restore_reader_(
    const ObCopyMacroBlockReaderInitParam &init_param,
    ObICopyMacroBlockReader *&reader)
{
  int ret = OB_SUCCESS;
  ObCopyMacroBlockRestoreReader *tmp_reader = NULL;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("physical copy task do not init", K(ret));
  } else if (!copy_ctx_->is_leader_restore_ || !init_param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get macro block restore reader get invalid argument", K(ret), KPC(copy_ctx_), K(init_param));
  } else {
    void *buf = mtl_malloc(sizeof(ObCopyMacroBlockRestoreReader), "MacroRestReader");
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory", K(ret), KP(buf));
    } else if (FALSE_IT(tmp_reader = new(buf) ObCopyMacroBlockRestoreReader())) {
    } else if (OB_FAIL(tmp_reader->init(init_param))) {
      STORAGE_LOG(WARN, "failed to init restore reader", K(ret), K(init_param), KPC(copy_ctx_));
    } else {
      reader = tmp_reader;
      tmp_reader = NULL;
    }

    if (OB_FAIL(ret)) {
      if (NULL != reader) {
        reader->~ObICopyMacroBlockReader();
        mtl_free(reader);
        reader = NULL;
      }
    }
    if (NULL != tmp_reader) {
      tmp_reader->~ObCopyMacroBlockRestoreReader();
      mtl_free(tmp_reader);
      tmp_reader = NULL;
    }
  }
  return ret;
}

int ObPhysicalCopyTask::get_macro_block_writer_(
    ObICopyMacroBlockReader *reader,
    ObIndexBlockRebuilder *index_block_rebuilder,
    ObStorageHAMacroBlockWriter *&writer)
{
  int ret = OB_SUCCESS;
  ObStorageHAMacroBlockWriter *tmp_writer = nullptr;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("physical copy task do not init", K(ret));
  } else if (OB_ISNULL(reader) || OB_ISNULL(index_block_rebuilder)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("macro block writer get invalid argument", K(ret), KP(reader), KP(index_block_rebuilder));
  } else {
    void *buf = mtl_malloc(sizeof(ObStorageHAMacroBlockWriter), "MacroObWriter");
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory", K(ret), KP(buf));
    } else if (FALSE_IT(writer = new(buf) ObStorageHAMacroBlockWriter())) {
    } else if (OB_FAIL(writer->init(copy_ctx_->tenant_id_, reader, index_block_rebuilder))) {
      STORAGE_LOG(WARN, "failed to init ob reader", K(ret), KPC(copy_ctx_));
    }

    if (OB_FAIL(ret)) {
      free_macro_block_writer_(writer);
    }
  }
  return ret;
}

int ObPhysicalCopyTask::generate_next_task(ObITask *&next_task)
{
  int ret = OB_SUCCESS;
  ObPhysicalCopyTask *tmp_next_task = nullptr;
  bool is_iter_end = false;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_ERROR("not init", K(ret));
  } else if (OB_FAIL(finish_task_->check_is_iter_end(is_iter_end))) {
    LOG_WARN("failed to check is iter end", K(ret));
  } else if (is_iter_end) {
    ret = OB_ITER_END;
  } else if (OB_FAIL(dag_->alloc_task(tmp_next_task))) {
    LOG_WARN("failed to alloc task", K(ret));
  } else if (OB_FAIL(tmp_next_task->init(copy_ctx_, finish_task_))) {
    LOG_WARN("failed to init next task", K(ret), K(*copy_ctx_));
  } else {
    next_task = tmp_next_task;
  }

  return ret;
}

void ObPhysicalCopyTask::free_macro_block_reader_(ObICopyMacroBlockReader *&reader)
{
  if (OB_NOT_NULL(reader)) {
    reader->~ObICopyMacroBlockReader();
    mtl_free(reader);
    reader = nullptr;
  }
}

void ObPhysicalCopyTask::free_macro_block_writer_(ObStorageHAMacroBlockWriter *&writer)
{
  if (OB_ISNULL(writer)) {
  } else {
    writer->~ObStorageHAMacroBlockWriter();
    mtl_free(writer);
    writer = NULL;
  }
}

int ObPhysicalCopyTask::build_copy_macro_block_reader_init_param_(
    ObCopyMacroBlockReaderInitParam &init_param)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("physical copy task do not init", K(ret));
  } else {
    init_param.tenant_id_ = copy_ctx_->tenant_id_;
    init_param.ls_id_ = copy_ctx_->ls_id_;
    init_param.table_key_ = copy_table_key_;
    init_param.is_leader_restore_ = copy_ctx_->is_leader_restore_;
    init_param.src_info_ = copy_ctx_->src_info_;
    init_param.bandwidth_throttle_ = copy_ctx_->bandwidth_throttle_;
    init_param.svr_rpc_proxy_ = copy_ctx_->svr_rpc_proxy_;
    init_param.restore_base_info_ = copy_ctx_->restore_base_info_;
    init_param.meta_index_store_ = copy_ctx_->meta_index_store_;
    init_param.second_meta_index_store_ = copy_ctx_->second_meta_index_store_;
    init_param.restore_macro_block_id_mgr_ = copy_ctx_->restore_macro_block_id_mgr_;
    init_param.copy_macro_range_info_ = copy_macro_range_info_;
    init_param.need_check_seq_ = copy_ctx_->need_check_seq_;
    init_param.ls_rebuild_seq_ = copy_ctx_->ls_rebuild_seq_;
    if (!init_param.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("copy macro block reader init param is invalid", K(ret), K(init_param));
    } else {
      LOG_INFO("succeed init param", KPC(copy_macro_range_info_), K(init_param));
    }
  }
  return ret;
}

int ObPhysicalCopyTask::record_server_event_()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(copy_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("copy ctx should not be null", K(ret), KPC_(copy_ctx));
  } else {
    SERVER_EVENT_ADD("storage_ha", "physical_copy_task",
        "tenant_id", copy_ctx_->tenant_id_,
        "ls_id", copy_ctx_->ls_id_.id(),
        "tablet_id", copy_ctx_->tablet_id_.id(),
        "macro_block_count", copy_macro_range_info_->macro_block_count_,
        "src", copy_ctx_->src_info_.src_addr_);
  }
  return ret;
}

/******************ObPhysicalCopyFinishTask*********************/
ObPhysicalCopyFinishTask::ObPhysicalCopyFinishTask()
  : ObITask(TASK_TYPE_MIGRATE_FINISH_PHYSICAL),
    is_inited_(false),
    copy_ctx_(),
    lock_(common::ObLatchIds::BACKUP_LOCK),
    sstable_param_(nullptr),
    sstable_macro_range_info_(),
    macro_range_info_index_(0),
    tablet_copy_finish_task_(nullptr),
    ls_(nullptr),
    tablet_service_(nullptr),
    sstable_index_builder_(),
    restore_macro_block_id_mgr_(nullptr)
{
}

ObPhysicalCopyFinishTask::~ObPhysicalCopyFinishTask()
{
  if (OB_NOT_NULL(restore_macro_block_id_mgr_)) {
    ob_delete(restore_macro_block_id_mgr_);
  }
}

int ObPhysicalCopyFinishTask::init(
    const ObPhysicalCopyTaskInitParam &init_param)
{
  int ret = OB_SUCCESS;
  common::ObInOutBandwidthThrottle *bandwidth_throttle = nullptr;
  ObLSService *ls_service = nullptr;
  ObStorageRpcProxy *svr_rpc_proxy = nullptr;
  ObStorageHADag *ha_dag = nullptr;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("physical copy finish task init twice", K(ret));
  } else if (!init_param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("physical copy finish task init get invalid argument", K(ret), K(init_param));
  } else if (OB_ISNULL(ls_service = (MTL(ObLSService *)))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls service should not be NULL", K(ret), KP(ls_service));
  } else if (FALSE_IT(bandwidth_throttle = GCTX.bandwidth_throttle_)) {
  } else if (FALSE_IT(svr_rpc_proxy = ls_service->get_storage_rpc_proxy())) {
  } else if (FALSE_IT(ha_dag = static_cast<ObStorageHADag *>(this->get_dag()))) {
  } else if (OB_FAIL(sstable_macro_range_info_.assign(init_param.sstable_macro_range_info_))) {
    LOG_WARN("failed to assign sstable macro range info", K(ret), K(init_param));
  } else if (OB_FAIL(build_restore_macro_block_id_mgr_(init_param))) {
    LOG_WARN("failed to build restore macro block id mgr", K(ret), K(init_param));
  } else {
    copy_ctx_.tenant_id_ = init_param.tenant_id_;
    copy_ctx_.ls_id_ = init_param.ls_id_;
    copy_ctx_.tablet_id_ = init_param.tablet_id_;
    copy_ctx_.src_info_ = init_param.src_info_;
    copy_ctx_.bandwidth_throttle_ = bandwidth_throttle;
    copy_ctx_.svr_rpc_proxy_ = svr_rpc_proxy;
    copy_ctx_.is_leader_restore_ = init_param.is_leader_restore_;
    copy_ctx_.restore_base_info_ = init_param.restore_base_info_;
    copy_ctx_.meta_index_store_ = init_param.meta_index_store_;
    copy_ctx_.second_meta_index_store_ = init_param.second_meta_index_store_;
    copy_ctx_.ha_dag_ = ha_dag;
    copy_ctx_.sstable_index_builder_ = &sstable_index_builder_;
    copy_ctx_.restore_macro_block_id_mgr_ = restore_macro_block_id_mgr_;
    copy_ctx_.need_check_seq_ = init_param.need_check_seq_;
    copy_ctx_.ls_rebuild_seq_ = init_param.ls_rebuild_seq_;
    macro_range_info_index_ = 0;
    ls_ = init_param.ls_;
    sstable_param_ = init_param.sstable_param_;
    tablet_copy_finish_task_ = init_param.tablet_copy_finish_task_;
    int64_t cluster_version = 0;
    if (OB_FAIL(get_cluster_version_(init_param, cluster_version))) {
      LOG_WARN("failed to get cluster version", K(ret));
    } else if (OB_FAIL(prepare_sstable_index_builder_(init_param.ls_id_,
        init_param.tablet_id_, init_param.sstable_param_, cluster_version))) {
      LOG_WARN("failed to prepare sstable index builder", K(ret), K(init_param), K(cluster_version));
    } else {
      is_inited_ = true;
      LOG_INFO("succeed init ObPhysicalCopyFinishTask", K(init_param), K(sstable_macro_range_info_));
    }
  }
  return ret;
}

int ObPhysicalCopyFinishTask::get_macro_block_copy_info(
    ObITable::TableKey &copy_table_key,
    const ObCopyMacroRangeInfo *&copy_macro_range_info)
{
  int ret = OB_SUCCESS;
  copy_table_key.reset();
  copy_macro_range_info = nullptr;
  ObMacroBlockCopyInfo macro_block_copy_info;
  ObMigrationFakeBlockID block_id;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("physical copy finish task do not init", K(ret));
  } else {
    common::SpinWLockGuard guard(lock_);
    if (macro_range_info_index_ == sstable_macro_range_info_.copy_macro_range_array_.count()) {
      ret = OB_ITER_END;
    } else {
      copy_table_key = sstable_macro_range_info_.copy_table_key_;
      copy_macro_range_info = &sstable_macro_range_info_.copy_macro_range_array_.at(macro_range_info_index_);
      macro_range_info_index_++;
      LOG_INFO("succeed get macro block copy info", K(copy_table_key), KPC(copy_macro_range_info),
          K(macro_range_info_index_), K(sstable_macro_range_info_));
    }
  }
  return ret;
}

int ObPhysicalCopyFinishTask::process()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("physical copy finish task do not init", K(ret));
  } else if (copy_ctx_.ha_dag_->get_ha_dag_net_ctx()->is_failed()) {
    FLOG_INFO("ha dag net is already failed, skip physical copy finish task", K(copy_ctx_));
  } else if (OB_FAIL(create_sstable_())) {
    LOG_WARN("failed to create sstable", K(ret), K(copy_ctx_));
  } else if (OB_FAIL(check_sstable_valid_())) {
    LOG_WARN("failed to check sstable valid", K(ret), K(copy_ctx_));
  } else {
    LOG_INFO("succeed physical copy finish", K(copy_ctx_));
  }

  if (OB_FAIL(ret)) {
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = ObStorageHADagUtils::deal_with_fo(ret, copy_ctx_.ha_dag_))) {
      LOG_WARN("failed to deal with fo", K(ret), K(tmp_ret), K(copy_ctx_));
    }
  }
  return ret;
}

int ObPhysicalCopyFinishTask::check_is_iter_end(bool &is_iter_end)
{
  int ret = OB_SUCCESS;
  is_iter_end = false;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("physical copy finish task do not init", K(ret));
  } else {
    common::SpinRLockGuard guard(lock_);
    if (macro_range_info_index_ == sstable_macro_range_info_.copy_macro_range_array_.count()) {
      is_iter_end = true;
    } else {
      is_iter_end = false;
    }
  }
  return ret;
}

int ObPhysicalCopyFinishTask::prepare_data_store_desc_(
    const share::ObLSID &ls_id,
    const common::ObTabletID &tablet_id,
    const ObMigrationSSTableParam *sstable_param,
    const int64_t cluster_version,
    ObDataStoreDesc &desc)
{
  int ret = OB_SUCCESS;
  desc.reset();
  ObTabletHandle tablet_handle;
  ObTablet *tablet = nullptr;
  ObMergeType merge_type;

  if (!tablet_id.is_valid() || cluster_version < 0 || OB_ISNULL(sstable_param)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("prepare sstable index builder get invalid argument", K(ret), K(tablet_id), K(cluster_version), KP(sstable_param));
  } else if (OB_FAIL(get_merge_type_(sstable_param, merge_type))) {
    LOG_WARN("failed to get merge type", K(ret), KPC(sstable_param));
  } else if (OB_FAIL(ls_->get_tablet(tablet_id, tablet_handle))) {
    LOG_WARN("failed to get tablet", K(ret), K(tablet_id));
  } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet should not be NULL", K(ret), K(tablet_id));
  } else if (OB_FAIL(desc.init(tablet->get_storage_schema(),
      ls_id,
      tablet_id,
      merge_type,
      tablet->get_snapshot_version(),
      cluster_version))) {
    LOG_WARN("failed to init index store desc", K(ret), K(tablet_id), K(merge_type), KPC(sstable_param));
  } else {
    const ObStorageSchema &storage_schema = tablet->get_storage_schema();
    desc.row_column_count_ = desc.rowkey_column_count_ + 1;
    desc.col_desc_array_.reset();
    desc.need_prebuild_bloomfilter_ = false;
    if (OB_FAIL(desc.col_desc_array_.init(desc.row_column_count_))) {
      LOG_WARN("failed to reserve column desc array", K(ret));
    } else if (OB_FAIL(storage_schema.get_rowkey_column_ids(desc.col_desc_array_))) {
      LOG_WARN("failed to get rowkey column ids", K(ret));
    } else if (OB_FAIL(ObMultiVersionRowkeyHelpper::add_extra_rowkey_cols(desc.col_desc_array_))) {
      LOG_WARN("failed to get extra rowkey column ids", K(ret));
    } else {
      ObObjMeta meta;
      meta.set_varchar();
      meta.set_collation_type(CS_TYPE_BINARY);
      share::schema::ObColDesc col;
      col.col_id_ = static_cast<uint64_t>(desc.row_column_count_ + OB_APP_MIN_COLUMN_ID);
      col.col_type_ = meta;
      col.col_order_ = DESC;
      if (OB_FAIL(desc.col_desc_array_.push_back(col))) {
        LOG_WARN("failed to push back last col for index", K(ret), K(col));
      }
    }
  }
  return ret;
}

int ObPhysicalCopyFinishTask::get_cluster_version_(
    const ObPhysicalCopyTaskInitParam &init_param,
    int64_t &cluster_version)
{
  int ret = OB_SUCCESS;
  if (!init_param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid args", K(ret), K(init_param));
  } else {
    // if restore use backup cluster version
    if (init_param.is_leader_restore_) {
      if (OB_ISNULL(init_param.restore_base_info_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("restore base info is null", K(ret), K(init_param));
      } else {
        cluster_version = init_param.restore_base_info_->backup_cluster_version_;
      }
    } else {
      // TODO(yangyi.yyy): refine get cluster version later
      cluster_version = static_cast<int64_t>(GET_MIN_CLUSTER_VERSION());
    }
  }
  return ret;
}

int ObPhysicalCopyFinishTask::prepare_sstable_index_builder_(
    const share::ObLSID &ls_id,
    const common::ObTabletID &tablet_id,
    const ObMigrationSSTableParam *sstable_param,
    const int64_t cluster_version)
{
  int ret = OB_SUCCESS;
  ObDataStoreDesc desc;
  const ObSSTableIndexBuilder::ObSpaceOptimizationMode mode = sstable_param->table_key_.is_ddl_sstable()
      ? ObSSTableIndexBuilder::DISABLE : ObSSTableIndexBuilder::AUTO;

  if (!tablet_id.is_valid() || OB_ISNULL(sstable_param)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("prepare sstable index builder get invalid argument", K(ret), K(tablet_id), KP(sstable_param));
  } else if (0 == sstable_param->basic_meta_.data_macro_block_count_) {
    LOG_INFO("sstable is empty, no need build sstable index builder", K(ret), K(tablet_id), KPC(sstable_param));
  } else if (OB_FAIL(prepare_data_store_desc_(ls_id, tablet_id, sstable_param, cluster_version, desc))) {
    LOG_WARN("failed to prepare data store desc", K(ret), K(tablet_id), K(cluster_version));
  } else if (OB_FAIL(sstable_index_builder_.init(
      desc,
      nullptr, // macro block flush callback, default value is nullptr
      mode))) {
    LOG_WARN("failed to init sstable index builder", K(ret), K(desc));
  }
  return ret;
}

int ObPhysicalCopyFinishTask::get_merge_type_(
    const ObMigrationSSTableParam *sstable_param,
    ObMergeType &merge_type)
{
  int ret = OB_SUCCESS;
  merge_type = ObMergeType::INVALID_MERGE_TYPE;

  if (OB_ISNULL(sstable_param)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("sstable should not be NULL", K(ret), KP(sstable_param));
  } else if (sstable_param->table_key_.is_major_sstable()) {
    merge_type = ObMergeType::MAJOR_MERGE;
  } else if (sstable_param->table_key_.is_minor_sstable()) {
    merge_type = ObMergeType::MINOR_MERGE;
  } else if (sstable_param->table_key_.is_ddl_sstable()) {
    merge_type = ObMergeType::DDL_KV_MERGE;
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sstable type is unexpected", K(ret), KPC(sstable_param));
  }
  return ret;
}

int ObPhysicalCopyFinishTask::create_sstable_()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("physical copy finish task do not init", K(ret));
  } else if (0 == sstable_param_->basic_meta_.data_macro_block_count_) {
    //create empty sstable
    if (OB_FAIL(create_empty_sstable_())) {
      LOG_WARN("failed to create empty sstable", K(ret), KPC(sstable_param_));
    }
  } else {
    if (OB_FAIL(create_sstable_with_index_builder_())) {
      LOG_WARN("failed to create sstable with index builder", K(ret), KPC(sstable_param_));
    }
  }
  return ret;
}

int ObPhysicalCopyFinishTask::create_empty_sstable_()
{
  int ret = OB_SUCCESS;
  ObTableHandleV2 table_handle;
  ObTabletCreateSSTableParam param;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("physical copy finish task do not init", K(ret));
  } else if (OB_FAIL(build_create_sstable_param_(param))) {
    LOG_WARN("failed to build create sstable param", K(ret));
  } else if (OB_FAIL(ObTabletCreateDeleteHelper::create_sstable(param, table_handle))) {
    LOG_WARN("failed to create sstable", K(ret), K(param), K(copy_ctx_));
  } else if (OB_FAIL(tablet_copy_finish_task_->add_sstable(table_handle))) {
    LOG_WARN("failed to add table handle", K(ret), K(table_handle), K(copy_ctx_));
  }
  return ret;
}

int ObPhysicalCopyFinishTask::create_sstable_with_index_builder_()
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  ObTablet *tablet = nullptr;
  int64_t column_count = 0;
  ObMergeType merge_type;
  ObSSTableMergeRes res;
  ObTableHandleV2 table_handle;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("physical copy finish task do not init", K(ret));
  } else {
    SMART_VAR(ObTabletCreateSSTableParam, param) {
      //TODO(lingchuan) column_count should not be in parameters
      if (OB_FAIL(get_merge_type_(sstable_param_, merge_type))) {
        LOG_WARN("failed to get merge type", K(ret), K(copy_ctx_));
      } else if (OB_FAIL(ls_->get_tablet(copy_ctx_.tablet_id_, tablet_handle))) {
        LOG_WARN("failed to get tablet", K(ret), K(copy_ctx_));
      } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("tablet should not be NULL", K(ret), K(copy_ctx_));
      } else if (FALSE_IT(column_count = sstable_param_->basic_meta_.column_cnt_)) {
      } else if (OB_FAIL(sstable_index_builder_.close(column_count, res))) {
        LOG_WARN("failed to close sstable index builder", K(ret), K(column_count), K(copy_ctx_));
      } else if (OB_FAIL(build_create_sstable_param_(tablet, res, param))) {
        LOG_WARN("failed to build create sstable param", K(ret), K(copy_ctx_));
      } else if (OB_FAIL(ObTabletCreateDeleteHelper::create_sstable(param, table_handle))) {
        LOG_WARN("failed to create sstable", K(ret), K(copy_ctx_), KPC(sstable_param_));
      } else if (OB_FAIL(tablet_copy_finish_task_->add_sstable(table_handle))) {
        LOG_WARN("failed to add table handle", K(ret), K(table_handle), K(copy_ctx_));
      }
    }
  }
  return ret;
}

int ObPhysicalCopyFinishTask::build_create_sstable_param_(
    ObTablet *tablet,
    const blocksstable::ObSSTableMergeRes &res,
    ObTabletCreateSSTableParam &param)
{
  //TODO(lingchuan) this param maker ObTablet class will be better to be safeguard
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("physical copy finish task do not init", K(ret));
  } else if (OB_UNLIKELY(OB_ISNULL(tablet) || !res.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("build create sstable param get invalid argument", K(ret), KP(tablet), K(res));
  } else {
    const ObStorageSchema &storage_schema = tablet->get_storage_schema();
    param.table_key_ = sstable_param_->table_key_;
    param.sstable_logic_seq_ = sstable_param_->basic_meta_.sstable_logic_seq_;
    param.schema_version_ = sstable_param_->basic_meta_.schema_version_;
    param.table_mode_ = sstable_param_->basic_meta_.table_mode_;
    param.index_type_ = static_cast<share::schema::ObIndexType>(sstable_param_->basic_meta_.index_type_);
    param.create_snapshot_version_ = sstable_param_->basic_meta_.create_snapshot_version_;
    param.progressive_merge_round_ = sstable_param_->basic_meta_.progressive_merge_round_;
    param.progressive_merge_step_ = sstable_param_->basic_meta_.progressive_merge_step_;

    ObSSTableMergeRes::fill_addr_and_data(res.root_desc_,
        param.root_block_addr_, param.root_block_data_);
    ObSSTableMergeRes::fill_addr_and_data(res.data_root_desc_,
        param.data_block_macro_meta_addr_, param.data_block_macro_meta_);
    param.is_meta_root_ = res.data_root_desc_.is_meta_root_;
    param.root_row_store_type_ = res.root_desc_.row_type_;
    param.data_index_tree_height_ = res.root_desc_.height_;
    param.index_blocks_cnt_ = res.index_blocks_cnt_;
    param.data_blocks_cnt_ = res.data_blocks_cnt_;
    param.micro_block_cnt_ = res.micro_block_cnt_;
    param.use_old_macro_block_count_ = res.use_old_macro_block_count_;
    param.row_count_ = res.row_count_;
    param.column_cnt_ = res.data_column_cnt_;
    param.data_checksum_ = res.data_checksum_;
    param.occupy_size_ = res.occupy_size_;
    param.original_size_ = res.original_size_;
    param.max_merged_trans_version_ = res.max_merged_trans_version_;
    param.contain_uncommitted_row_ = res.contain_uncommitted_row_;
    param.compressor_type_ = res.compressor_type_;
    param.encrypt_id_ = res.encrypt_id_;
    param.master_key_id_ = res.master_key_id_;
    param.nested_size_ = res.nested_size_;
    param.nested_offset_ = res.nested_offset_;
    param.data_block_ids_ = res.data_block_ids_;
    param.other_block_ids_ = res.other_block_ids_;
    param.rowkey_column_cnt_ = sstable_param_->basic_meta_.rowkey_column_count_;
    param.ddl_scn_ = sstable_param_->basic_meta_.ddl_scn_;
    MEMCPY(param.encrypt_key_, res.encrypt_key_, share::OB_MAX_TABLESPACE_ENCRYPT_KEY_LENGTH);
    if (param.table_key_.is_major_sstable() || param.table_key_.is_ddl_sstable()) {
      if (OB_FAIL(res.fill_column_checksum(sstable_param_->column_default_checksums_,
          param.column_checksums_))) {
        LOG_WARN("fail to fill column checksum", K(ret), K(res));
      }
    }
  }
  return ret;
}

int ObPhysicalCopyFinishTask::build_create_sstable_param_(
    ObTabletCreateSSTableParam &param)
{
  //TODO(lingchuan) this param maker ObTablet class will be better to be safeguard
  //using sstable meta to create sstable
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("phyiscal copy finish task do not init", K(ret));
  } else if (0 != sstable_param_->basic_meta_.data_macro_block_count_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sstable param has data macro block, can not build sstable from basic meta", K(ret), KPC(sstable_param_));
  } else {
    param.table_key_ = sstable_param_->table_key_;
    param.sstable_logic_seq_ = sstable_param_->basic_meta_.sstable_logic_seq_;
    param.schema_version_ = sstable_param_->basic_meta_.schema_version_;
    param.create_snapshot_version_ = sstable_param_->basic_meta_.create_snapshot_version_;
    param.table_mode_ = sstable_param_->basic_meta_.table_mode_;
    param.index_type_ = static_cast<share::schema::ObIndexType>(sstable_param_->basic_meta_.index_type_);
    param.progressive_merge_round_ = sstable_param_->basic_meta_.progressive_merge_round_;
    param.progressive_merge_step_ = sstable_param_->basic_meta_.progressive_merge_step_;
    param.is_ready_for_read_ = true;
    param.root_row_store_type_ = sstable_param_->basic_meta_.row_store_type_;
    param.index_blocks_cnt_ = sstable_param_->basic_meta_.index_macro_block_count_;
    param.data_blocks_cnt_ = sstable_param_->basic_meta_.data_macro_block_count_;
    param.micro_block_cnt_ = sstable_param_->basic_meta_.data_micro_block_count_;
    param.use_old_macro_block_count_ = sstable_param_->basic_meta_.use_old_macro_block_count_;
    param.row_count_ = sstable_param_->basic_meta_.row_count_;
    param.column_cnt_ = sstable_param_->basic_meta_.column_cnt_;
    param.data_checksum_ = sstable_param_->basic_meta_.data_checksum_;
    param.occupy_size_ = sstable_param_->basic_meta_.occupy_size_;
    param.original_size_ = sstable_param_->basic_meta_.original_size_;
    param.max_merged_trans_version_ = sstable_param_->basic_meta_.max_merged_trans_version_;
    param.ddl_scn_ = sstable_param_->basic_meta_.ddl_scn_;
    param.filled_tx_scn_ = sstable_param_->basic_meta_.filled_tx_scn_;
    param.contain_uncommitted_row_ = sstable_param_->basic_meta_.contain_uncommitted_row_;
    param.compressor_type_ = sstable_param_->basic_meta_.compressor_type_;
    param.encrypt_id_ = sstable_param_->basic_meta_.encrypt_id_;
    param.master_key_id_ = sstable_param_->basic_meta_.master_key_id_;
    param.root_block_addr_.set_none_addr();
    param.data_block_macro_meta_addr_.set_none_addr();
    param.rowkey_column_cnt_ = sstable_param_->basic_meta_.rowkey_column_count_;
    MEMCPY(param.encrypt_key_, sstable_param_->basic_meta_.encrypt_key_, share::OB_MAX_TABLESPACE_ENCRYPT_KEY_LENGTH);
    if (OB_FAIL(param.column_checksums_.assign(sstable_param_->column_checksums_))) {
      LOG_WARN("fail to assign column checksums", K(ret), KPC(sstable_param_));
    }
  }
  return ret;
}

int ObPhysicalCopyFinishTask::build_restore_macro_block_id_mgr_(
    const ObPhysicalCopyTaskInitParam &init_param)
{
  int ret = OB_SUCCESS;
  ObRestoreMacroBlockIdMgr *restore_macro_block_id_mgr = nullptr;

  if (!init_param.is_leader_restore_) {
    restore_macro_block_id_mgr_ = nullptr;
  } else {
    void *buf = mtl_malloc(sizeof(ObRestoreMacroBlockIdMgr), "RestoreMacIdMgr");
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory", K(ret), KP(buf));
    } else if (FALSE_IT(restore_macro_block_id_mgr = new(buf) ObRestoreMacroBlockIdMgr())) {
    } else if (OB_FAIL(restore_macro_block_id_mgr->init(init_param.ls_id_, init_param.tablet_id_,
        init_param.sstable_macro_range_info_.copy_table_key_,
        *init_param.restore_base_info_, *init_param.second_meta_index_store_))) {
      STORAGE_LOG(WARN, "failed to init restore macro block id mgr", K(ret), K(init_param));
    } else {
      restore_macro_block_id_mgr_ = restore_macro_block_id_mgr;
      restore_macro_block_id_mgr = NULL;
    }

    if (OB_FAIL(ret)) {
      if (NULL != restore_macro_block_id_mgr_) {
        restore_macro_block_id_mgr_->~ObRestoreMacroBlockIdMgr();
        mtl_free(restore_macro_block_id_mgr_);
        restore_macro_block_id_mgr_ = nullptr;
      }
    }
    if (NULL != restore_macro_block_id_mgr) {
      restore_macro_block_id_mgr->~ObRestoreMacroBlockIdMgr();
      mtl_free(restore_macro_block_id_mgr);
      restore_macro_block_id_mgr = nullptr;
    }
  }
  return ret;
}

int ObPhysicalCopyFinishTask::check_sstable_valid_()
{
  int ret = OB_SUCCESS;
  ObTableHandleV2 table_handle;
  ObSSTable *sstable = nullptr;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("phyiscal copy finish task do not init", K(ret));
  } else if (OB_FAIL(tablet_copy_finish_task_->get_sstable(sstable_param_->table_key_, table_handle))) {
    LOG_WARN("failed to get sstable", K(ret), KPC(sstable_param_));
  } else if (OB_FAIL(table_handle.get_sstable(sstable))) {
    LOG_WARN("failed to get sstable", K(ret), KPC(sstable_param_));
  } else if (OB_ISNULL(sstable)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sstable should not be NULL", K(ret), KP(sstable), KPC(sstable_param_));
  } else if (OB_FAIL(check_sstable_meta_(*sstable_param_, sstable->get_meta()))) {
    LOG_WARN("failed to check sstable meta", K(ret), KPC(sstable), KPC(sstable_param_));
  }
  return ret;
}

int ObPhysicalCopyFinishTask::check_sstable_meta_(
    const ObMigrationSSTableParam &src_meta,
    const ObSSTableMeta &write_meta)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("phyiscal copy finish task do not init", K(ret));
  } else if (!src_meta.is_valid() || !write_meta.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("check sstable meta get invalid argument", K(ret), K(src_meta), K(write_meta));
  } else if (OB_FAIL(ObSSTableMetaChecker::check_sstable_meta(src_meta, write_meta))) {
    LOG_WARN("failed to check sstable meta", K(ret), K(src_meta), K(write_meta));
  }
  return ret;
}

/******************ObTabletCopyFinishTask*********************/
ObTabletCopyFinishTask::ObTabletCopyFinishTask()
  : ObITask(TASK_TYPE_MIGRATE_FINISH_PHYSICAL),
    is_inited_(false),
    lock_(common::ObLatchIds::BACKUP_LOCK),
    tablet_id_(),
    ls_(nullptr),
    reporter_(nullptr),
    ha_dag_(nullptr),
    minor_tables_handle_(),
    ddl_tables_handle_(),
    major_tables_handle_(),
    restore_action_(ObTabletRestoreAction::MAX),
    src_tablet_meta_(nullptr)

{
}

ObTabletCopyFinishTask::~ObTabletCopyFinishTask()
{
}

int ObTabletCopyFinishTask::init(
    const common::ObTabletID &tablet_id,
    ObLS *ls,
    observer::ObIMetaReport *reporter,
    const ObTabletRestoreAction::ACTION &restore_action,
    const ObMigrationTabletParam *src_tablet_meta)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("tablet copy finish task init twice", K(ret));
  } else if (!tablet_id.is_valid() || OB_ISNULL(ls) || OB_ISNULL(reporter) || OB_ISNULL(src_tablet_meta)
      || !ObTabletRestoreAction::is_valid(restore_action)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("init tablet copy finish task get invalid argument", K(ret), K(tablet_id), KP(ls),
        KP(reporter), KP(src_tablet_meta), K(restore_action));
  } else {
    tablet_id_ = tablet_id;
    ls_ = ls;
    reporter_ = reporter;
    ha_dag_ = static_cast<ObStorageHADag *>(this->get_dag());
    restore_action_ = restore_action;
    src_tablet_meta_ = src_tablet_meta;
    is_inited_ = true;
  }
  return ret;
}

int ObTabletCopyFinishTask::process()
{
  int ret = OB_SUCCESS;
  bool only_contain_major = false;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet copy finish task do not init", K(ret));
  } else if (ha_dag_->get_ha_dag_net_ctx()->is_failed()) {
    FLOG_INFO("ha dag net is already failed, skip physical copy finish task", K(tablet_id_), KPC(ha_dag_));
  } else if (OB_FAIL(create_new_table_store_with_major_())) {
    LOG_WARN("failed to create new table store with major", K(ret), K_(tablet_id));
  } else if (OB_FAIL(create_new_table_store_with_ddl_())) {
    LOG_WARN("failed to create new table store with ddl", K(ret), K_(tablet_id));
  } else if (OB_FAIL(create_new_table_store_with_minor_())) {
    LOG_WARN("failed to create new table store with minor", K(ret), K_(tablet_id));
  } else if (OB_FAIL(update_tablet_data_status_())) {
    LOG_WARN("failed to update tablet data status", K(ret), K(tablet_id_));
  }

  if (OB_FAIL(ret)) {
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = ObStorageHADagUtils::deal_with_fo(ret, ha_dag_))) {
      LOG_WARN("failed to deal with fo", K(ret), K(tmp_ret), K(tablet_id_));
    }
  }
  return ret;
}

int ObTabletCopyFinishTask::add_sstable(ObTableHandleV2 &table_handle)
{
  int ret = OB_SUCCESS;
  ObTablesHandleArray *tables_handle_ptr = nullptr;
  common::SpinWLockGuard guard(lock_);
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet copy finish task do not init", K(ret));
  } else if (!table_handle.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("add sstable get invalid argument", K(ret), K(table_handle));
  } else if (OB_FAIL(get_tables_handle_ptr_(table_handle.get_table()->get_key(), tables_handle_ptr))) {
    LOG_WARN("failed to get tables handle ptr", K(ret), K(table_handle));
  } else if (OB_FAIL(tables_handle_ptr->add_table(table_handle))) {
    LOG_WARN("failed to add table", K(ret), K(table_handle));
  }
  return ret;
}

int ObTabletCopyFinishTask::get_sstable(
    const ObITable::TableKey &table_key,
    ObTableHandleV2 &table_handle)
{
  int ret = OB_SUCCESS;
  ObTenantMetaMemMgr *meta_mem_mgr = nullptr;
  bool found = false;
  ObTablesHandleArray *tables_handle_ptr = nullptr;
  common::SpinRLockGuard guard(lock_);

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet copy finish task do not init", K(ret));
  } else if (!table_key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get sstable get invalid argument", K(ret), K(table_key));
  } else if (OB_ISNULL(meta_mem_mgr = MTL(ObTenantMetaMemMgr *))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get meta mem mgr from MTL", K(ret));
  } else if (OB_FAIL(get_tables_handle_ptr_(table_key, tables_handle_ptr))) {
    LOG_WARN("failed to get tables handle ptr", K(ret), K(table_key));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < tables_handle_ptr->get_count() && !found; ++i) {
      ObITable *table = tables_handle_ptr->get_table(i);
      if (OB_ISNULL(table)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table should not be NULL", K(ret), KP(table));
      } else if (table->get_key() == table_key) {
        if (OB_FAIL(table_handle.set_table(table, meta_mem_mgr, table_key.table_type_))) {
          LOG_WARN("failed to set table", K(ret), KPC(table), K(table_key));
        } else {
          found = true;
        }
      }
    }

    if (OB_SUCC(ret) && !found) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("can not get sstable, unexected", K(ret), K(table_key), K(major_tables_handle_),
          K(minor_tables_handle_), K(ddl_tables_handle_));
    }
  }
  return ret;
}

int ObTabletCopyFinishTask::inner_update_tablet_table_store_with_minor_(
    const bool &need_tablet_meta_merge,
    ObTablesHandleArray *tables_handle_ptr)
{
  int ret = OB_SUCCESS;
  ObBatchUpdateTableStoreParam update_table_store_param;
  ObTabletHandle tablet_handle;
  ObTablet *tablet = nullptr;
  const bool is_rollback = false;
  bool need_merge = false;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet copy finish task do not init", K(ret));
  } else if (OB_FAIL(ls_->get_tablet(tablet_id_, tablet_handle))) {
    LOG_WARN("failed to get tablet", K(ret), K(tablet_id_));
  } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet should not be NULL", K(ret), K(tablet_id_));
  } else if (need_tablet_meta_merge && OB_FAIL(check_need_merge_tablet_meta_(tablet, need_merge))) {
    LOG_WARN("failedto check remote logical sstable exist", K(ret), KPC(tablet));
  } else {
    update_table_store_param.multi_version_start_ = 0;
    update_table_store_param.tablet_meta_ = need_merge ? src_tablet_meta_ : nullptr;
    update_table_store_param.rebuild_seq_ = ls_->get_rebuild_seq();

    if (OB_FAIL(update_table_store_param.tables_handle_.assign(*tables_handle_ptr))) {
      LOG_WARN("failed to assign tables handle", K(ret), KPC(tables_handle_ptr));
    } else if (update_table_store_param.tables_handle_.empty()) {
      LOG_INFO("tablet do not has sstable", K(ret), K(tablet_id_), KPC(tables_handle_ptr), KPC(tablet));
    } else if (OB_FAIL(ls_->build_ha_tablet_new_table_store(tablet_id_, update_table_store_param))) {
      LOG_WARN("failed to build ha tablet new table store", K(ret), K(tablet_id_), KPC_(src_tablet_meta), K(update_table_store_param));
    }

    if (OB_FAIL(ret)) {
    } else if (tablet->get_tablet_meta().has_next_tablet_
        && OB_FAIL(ls_->trim_rebuild_tablet(tablet_id_, is_rollback))) {
      LOG_WARN("failed to trim rebuild tablet", K(ret), K(tablet_id_));
    }
  }
  return ret;
}

int ObTabletCopyFinishTask::create_new_table_store_with_ddl_()
{
  int ret = OB_SUCCESS;
  ObTablesHandleArray *tables_handle_ptr = &ddl_tables_handle_;
  const bool need_tablet_meta_merge = false;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet copy finish task do not init", K(ret));
  } else if (OB_FAIL(inner_update_tablet_table_store_with_minor_(need_tablet_meta_merge, tables_handle_ptr))) {
    LOG_WARN("failed to update tablet table store with minor", K(ret));
  }
  return ret;
}

int ObTabletCopyFinishTask::create_new_table_store_with_minor_()
{
  int ret = OB_SUCCESS;
  ObTablesHandleArray *tables_handle_ptr = &minor_tables_handle_;
  const bool need_tablet_meta_merge = true;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet copy finish task do not init", K(ret));
  } else if (OB_FAIL(inner_update_tablet_table_store_with_minor_(need_tablet_meta_merge, tables_handle_ptr))) {
    LOG_WARN("failed to update tablet table store with minor", K(ret));
  }
  return ret;
}

int ObTabletCopyFinishTask::create_new_table_store_with_major_()
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  ObTablet *tablet = nullptr;
  const bool is_rollback = false;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet copy finish task do not init", K(ret));
  } else if (OB_ISNULL(src_tablet_meta_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("src tablet meta should not be null", K(ret));
  } else if (OB_FAIL(ls_->get_tablet(tablet_id_, tablet_handle))) {
    LOG_WARN("failed to get tablet", K(ret), K(tablet_id_));
  } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet should not be NULL", K(ret), K(tablet_id_));
  } else if (major_tables_handle_.empty()) {
    // do nothing
  } else if (ObTabletRestoreAction::is_restore_major(restore_action_) && 1 != major_tables_handle_.get_count() ) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("major tablet should only has one sstable", K(ret), "major_sstable_count", major_tables_handle_.get_count(), K(major_tables_handle_));
  } else {
    ObSEArray<ObITable *, MAX_SSTABLE_CNT_IN_STORAGE> major_tables;
    if (OB_FAIL(major_tables_handle_.get_tables(major_tables))) {
      LOG_WARN("failed to get tables", K(ret));
    } else if (OB_FAIL(ObTableStoreUtil::sort_major_tables(major_tables))) {
      LOG_WARN("failed to sort mjaor tables", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < major_tables.count(); ++i) {
        ObITable *table_ptr = major_tables.at(i);
        if (OB_ISNULL(table_ptr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("table ptr should not be null", K(ret), KP(table_ptr));
        } else if (!table_ptr->is_major_sstable()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("table ptr is not major", K(ret), KPC(table_ptr));
        } else if (OB_FAIL(inner_update_tablet_table_store_with_major_(tablet_id_, table_ptr))) {
          LOG_WARN("failed to update tablet table store", K(ret), K_(tablet_id), KPC(table_ptr));
        }
      }
    }

    if (OB_FAIL(ret)) {
    } else if (tablet->get_tablet_meta().has_next_tablet_
        && OB_FAIL(ls_->trim_rebuild_tablet(tablet_id_, is_rollback))) {
      LOG_WARN("failed to trim rebuild tablet", K(ret), K(tablet_id_));
    }
  }
  return ret;
}

int ObTabletCopyFinishTask::inner_update_tablet_table_store_with_major_(
    const common::ObTabletID &tablet_id,
    ObITable *table_ptr)
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  ObTablet *tablet = nullptr;
  ObTableHandleV2 table_handle_v2;
  SCN tablet_snapshot_version;
  ObTenantMetaMemMgr *meta_mem_mgr = nullptr;
  if (!tablet_id.is_valid() || OB_ISNULL(table_ptr) || OB_ISNULL(src_tablet_meta_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table ptr should not be null", K(ret), K(tablet_id), KP(table_ptr), KP(src_tablet_meta_));
  } else if (OB_FAIL(ls_->get_tablet(tablet_id, tablet_handle))) {
    LOG_WARN("failed to get tablet", K(ret), K(tablet_id));
  } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet should not be NULL", K(ret), K(tablet_id));
  } else if (OB_ISNULL(meta_mem_mgr = MTL(ObTenantMetaMemMgr *))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get meta mem mgr from MTL", K(ret));
  } else if (OB_FAIL(table_handle_v2.set_table(table_ptr, meta_mem_mgr, table_ptr->get_key().table_type_))) {
    LOG_WARN("failed to set table handle v2", K(ret), KPC(table_ptr));
  } else if (OB_FAIL(tablet->get_snapshot_version(tablet_snapshot_version))) {
    LOG_WARN("failed to get_snapshot_version", K(ret));
  } else {
    const int64_t update_snapshot_version = MAX(tablet->get_snapshot_version(), table_ptr->get_key().get_snapshot_version());
    const int64_t update_multi_version_start = MAX(tablet->get_multi_version_start(), table_ptr->get_key().get_snapshot_version());
    ObUpdateTableStoreParam param(table_handle_v2,
                            update_snapshot_version,
                            update_multi_version_start,
                            &src_tablet_meta_->storage_schema_,
                            ls_->get_rebuild_seq(),
                            true/*need_report*/,
                            SCN::min_scn()/*clog_checkpoint_scn*/,
                            true/*need_check_sstable*/,
                            true/*allow_duplicate_sstable*/,
                            &src_tablet_meta_->medium_info_list_);
    if (tablet->get_storage_schema().get_version() < src_tablet_meta_->storage_schema_.get_version()) {
      SERVER_EVENT_ADD("storage_ha", "schema_change_need_merge_tablet_meta",
                      "tenant_id", MTL_ID(),
                      "tablet_id", tablet_id.id(),
                      "old_schema_version", tablet->get_storage_schema().get_version(),
                      "new_schema_version", src_tablet_meta_->storage_schema_.get_version());
    }
#ifdef ERRSIM
    SERVER_EVENT_ADD("storage_ha", "update_major_tablet_table_store",
                      "tablet_id", tablet_id.id(),
                      "old_multi_version_start", tablet->get_multi_version_start(),
                      "new_multi_version_start", src_tablet_meta_->multi_version_start_,
                      "old_snapshot_version", tablet->get_snapshot_version(),
                      "new_snapshot_version", table_ptr->get_key().get_snapshot_version());
#endif
    if (FAILEDx(ls_->update_tablet_table_store(tablet_id, param, tablet_handle))) {
      LOG_WARN("failed to build ha tablet new table store", K(ret), K(tablet_id), K(param));
    }
  }
  return ret;
}

int ObTabletCopyFinishTask::update_tablet_data_status_()
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  ObTablet *tablet = nullptr;
  const ObTabletDataStatus::STATUS data_status = ObTabletDataStatus::COMPLETE;
  bool is_logical_sstable_exist = false;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet copy finish task do not init", K(ret));
  } else if (OB_FAIL(ls_->get_tablet(tablet_id_, tablet_handle))) {
    LOG_WARN("failed to get tablet", K(ret), K(tablet_id_));
  } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet should not be NULL", K(ret), K(tablet_id_), KP(tablet));
  } else if (tablet->get_tablet_meta().has_next_tablet_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet here should only has one", K(ret), KPC(tablet));
  } else if (tablet->get_tablet_meta().ha_status_.is_data_status_complete()) {
    //do nothing
  } else if (OB_FAIL(check_remote_logical_sstable_exist_(tablet, is_logical_sstable_exist))) {
    LOG_WARN("failedto check remote logical sstable exist", K(ret), KPC(tablet));
  } else if (is_logical_sstable_exist && tablet->get_tablet_meta().ha_status_.is_restore_status_full()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet still has remote logical sstable, unexpected !!!", K(ret), KPC(tablet));
  } else {
    const ObSSTableArray &major_sstables = tablet->get_table_store().get_major_sstables();
    if (OB_SUCC(ret)
        && tablet->get_tablet_meta().table_store_flag_.with_major_sstable()
        && tablet->get_tablet_meta().ha_status_.is_restore_status_full()
        && major_sstables.empty()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet should has major sstable, unexpected", K(ret), KPC(tablet), K(major_sstables));
    }

#ifdef ERRSIM
    if (OB_SUCC(ret)) {
      ret = E(EventTable::EN_UPDATE_TABLET_HA_STATUS_FAILED) OB_SUCCESS;
      if (OB_FAIL(ret)) {
        STORAGE_LOG(ERROR, "fake EN_UPDATE_TABLET_HA_STATUS_FAILED", K(ret));
      }
    }
#endif

    if (OB_SUCC(ret)) {
      if (!tablet->get_tablet_meta().ha_status_.is_restore_status_full()) {
        LOG_INFO("tablet is in restore status, do not update data stauts is full", K(ret), K(tablet_id_));
      } else if (OB_FAIL(ls_->update_tablet_ha_data_status(tablet_id_, data_status))) {
        LOG_WARN("[HA]failed to update tablet ha data status", K(ret), K(tablet_id_), K(data_status));
      }
    }

    if (OB_SUCC(ret)) {
      int tmp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (tmp_ret = reporter_->submit_tablet_update_task(ls_->get_tenant_id(),
          ls_->get_ls_id(), tablet_id_))) {
        LOG_WARN("failed to submit tablet update task", K(tmp_ret), KPC(ls_), K(tablet_id_));
      }
    }
  }
  return ret;
}

int ObTabletCopyFinishTask::check_need_merge_tablet_meta_(
    ObTablet *tablet,
    bool &need_merge)
{
  int ret = OB_SUCCESS;
  need_merge = false;
  bool is_exist = false;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet copy finish task do not init", K(ret));
  } else if (OB_ISNULL(tablet)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("check need merge tablet meta get invalid argument", K(ret), KP(tablet));
  } else if (tablet->get_tablet_meta().clog_checkpoint_scn_ >= src_tablet_meta_->clog_checkpoint_scn_) {
    need_merge = false;
  } else if (OB_FAIL(check_remote_logical_sstable_exist_(tablet, is_exist))) {
    LOG_WARN("failed to check remote logical sstable exist", K(ret), KPC(tablet));
  } else if (!is_exist) {
    need_merge = false;
  } else {
    need_merge = true;
  }
  return ret;
}

int ObTabletCopyFinishTask::check_remote_logical_sstable_exist_(
    ObTablet *tablet,
    bool &is_exist)
{
  int ret = OB_SUCCESS;
  is_exist = false;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet copy finish task do not init", K(ret));
  } else if (OB_ISNULL(tablet)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("check remote logical sstable exist get invalid argument", K(ret), KP(tablet));
  } else {
    const ObSSTableArray &minor_sstables = tablet->get_table_store().get_minor_sstables();
    for (int64_t i = 0; OB_SUCC(ret) && i < minor_sstables.count(); ++i) {
      const ObITable *table = minor_sstables.array_[i];
      if (OB_ISNULL(table)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("minor sstable should not be NULL", K(ret), KP(table));
      } else if (table->is_remote_logical_minor_sstable()) {
        is_exist = true;
        break;
      }
    }
  }
  return ret;
}

int ObTabletCopyFinishTask::get_tables_handle_ptr_(
    const ObITable::TableKey &table_key,
    ObTablesHandleArray *&tables_handle_ptr)
{
  int ret = OB_SUCCESS;
  tables_handle_ptr = nullptr;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet copy finish task do not init", K(ret));
  } else if (!table_key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get tables handle ptr get invalid argument", K(ret), K(table_key));
  } else if (table_key.is_major_sstable()) {
    tables_handle_ptr = &major_tables_handle_;
  } else if (table_key.is_minor_sstable()) {
    tables_handle_ptr = &minor_tables_handle_;
  } else if (table_key.is_ddl_sstable()) {
    tables_handle_ptr = &ddl_tables_handle_;
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get tables handle ptr get unexpected table key", K(ret), K(table_key));
  }
  return ret;
}

}
}

