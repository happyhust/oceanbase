/*
 * (C) Copyright 2022 Alipay Inc. All Rights Reserved.
 * Authors:
 *     Danling <fengjingkun.fjk@antgroup.com>
 */
#define USING_LOG_PREFIX STORAGE

#include "lib/oblog/ob_log_module.h"
#include "share/ob_force_print_log.h"
#include "share/ob_thread_mgr.h"
#include "storage/ob_tenant_tablet_stat_mgr.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::storage;


/************************************* ObTabletStatKey *************************************/
ObTabletStatKey::ObTabletStatKey(
  const int64_t ls_id,
  const uint64_t tablet_id)
  : ls_id_(ls_id),
    tablet_id_(tablet_id)
{
}

ObTabletStatKey::ObTabletStatKey(
  const share::ObLSID ls_id,
  const ObTabletID tablet_id)
  : ls_id_(ls_id),
    tablet_id_(tablet_id)
{
}

ObTabletStatKey::~ObTabletStatKey()
{
}

void ObTabletStatKey::reset()
{
  ls_id_.reset();
  tablet_id_.reset();
}

uint64_t ObTabletStatKey::hash() const
{
  uint64_t hash_val = 0;
  hash_val += ls_id_.hash();
  hash_val += tablet_id_.hash();
  return hash_val;
}

bool ObTabletStatKey::is_valid() const
{
  return ls_id_.is_valid() && tablet_id_.is_valid();
}

bool ObTabletStatKey::operator==(const ObTabletStatKey &other) const
{
  bool bret = true;
  if (this == &other) {
  } else if (ls_id_ != other.ls_id_ || tablet_id_ != other.tablet_id_) {
    bret = false;
  }
  return bret;
}

bool ObTabletStatKey::operator!=(const ObTabletStatKey &other) const
{
  return !(*this == other);
}


/************************************* ObTabletStat *************************************/
bool ObTabletStat::is_valid() const
{
  return ls_id_ > 0 && tablet_id_ > 0;
}

bool ObTabletStat::is_empty_query() const
{
  bool bret = false;
  if (0 == scan_physical_row_cnt_ && 0 == scan_micro_block_cnt_) {
    bret = true;
  }
  return bret;
}

ObTabletStat& ObTabletStat::operator=(const ObTabletStat &other)
{
  if (this != &other) {
    MEMCPY(this, &other, sizeof(ObTabletStat));
  }
  return *this;
}

ObTabletStat& ObTabletStat::operator+=(const ObTabletStat &other)
{
  if (other.is_valid()) {
    ls_id_ = other.ls_id_;
    tablet_id_ = other.tablet_id_;
    query_cnt_ += other.query_cnt_;
    merge_cnt_ += other.merge_cnt_;
    scan_logical_row_cnt_ += other.scan_logical_row_cnt_;
    scan_physical_row_cnt_ += other.scan_physical_row_cnt_;
    scan_micro_block_cnt_ += other.scan_micro_block_cnt_;
    pushdown_micro_block_cnt_ += other.pushdown_micro_block_cnt_;
    exist_row_total_table_cnt_ += other.exist_row_total_table_cnt_;
    exist_row_read_table_cnt_ += other.exist_row_read_table_cnt_;
    merge_physical_row_cnt_ += other.merge_physical_row_cnt_;
    merge_logical_row_cnt_ += other.merge_logical_row_cnt_;
  }
  return *this;
}

ObTabletStat& ObTabletStat::archive(int64_t factor)
{
  if (factor > 0) {
    query_cnt_ /= factor;
    merge_cnt_ /= factor;
    scan_logical_row_cnt_ /= factor;
    scan_physical_row_cnt_ /= factor;
    scan_micro_block_cnt_ /= factor;
    pushdown_micro_block_cnt_ /= factor;
    exist_row_total_table_cnt_ /= factor;
    exist_row_read_table_cnt_ /= factor;
    merge_physical_row_cnt_ /= factor;
    merge_logical_row_cnt_ /= factor;
  }
  return *this;
}

bool ObTabletStat::is_hot_tablet() const
{
  return query_cnt_ + merge_cnt_ >= ACCESS_FREQUENCY;
}

bool ObTabletStat::is_insert_mostly() const
{
  bool bret = false;
  if (merge_physical_row_cnt_ < BASIC_ROW_CNT_THRESHOLD) {
  } else {
    bret = merge_logical_row_cnt_ >= (merge_physical_row_cnt_ / BASE_FACTOR * INSERT_PIVOT_FACTOR);
  }
  return bret;
}

bool ObTabletStat::is_update_mostly() const
{
  bool bret = false;
  if (0 == merge_physical_row_cnt_ || merge_physical_row_cnt_ < BASIC_ROW_CNT_THRESHOLD) {
  } else {
    bret = merge_logical_row_cnt_ >= (merge_physical_row_cnt_ / BASE_FACTOR * UPDATE_PIVOT_FACTOR);
  }
  return bret;
}

bool ObTabletStat::is_inefficient_scan() const
{
  bool bret = false;
  if (0 == scan_logical_row_cnt_ || scan_logical_row_cnt_ < BASIC_ROW_CNT_THRESHOLD) {
  } else {
    bret = scan_physical_row_cnt_ / scan_logical_row_cnt_ >= SCAN_READ_FACTOR;
  }
  return bret;
}

bool ObTabletStat::is_inefficient_insert() const
{
  bool bret = false;
  if (0 == exist_row_total_table_cnt_ || exist_row_total_table_cnt_ < BASIC_TABLE_CNT_THRESHOLD) {
  } else {
    bret = exist_row_read_table_cnt_ * BASE_FACTOR / exist_row_total_table_cnt_ >= EXIST_READ_FACTOR;
  }
  return bret;
}

bool ObTabletStat::is_inefficient_pushdown() const
{
  bool bret = false;
  if (0 == scan_micro_block_cnt_ || scan_micro_block_cnt_ < BASIC_MICRO_BLOCK_CNT_THRESHOLD) {
  } else {
    bret = pushdown_micro_block_cnt_ < scan_micro_block_cnt_ / SCAN_READ_FACTOR;
  }
  return bret;
}


/************************************* ObTabletStream *************************************/
ObTabletStream::ObTabletStream()
  : key_(),
    curr_buckets_(CURR_BUCKET_STEP),
    latest_buckets_(LATEST_BUCKET_STEP),
    past_buckets_(PAST_BUCKET_STEP)
{
}

ObTabletStream::~ObTabletStream()
{
}

void ObTabletStream::reset()
{
  key_.reset();
  curr_buckets_.reset();
  latest_buckets_.reset();
  past_buckets_.reset();
}

void ObTabletStream::add_stat(const ObTabletStat &stat)
{
  if (!key_.is_valid()) {
    key_.ls_id_ = stat.ls_id_;
    key_.tablet_id_ = stat.tablet_id_;
  }

  if (key_.ls_id_.id() == stat.ls_id_ && key_.tablet_id_.id() == stat.tablet_id_) {
    curr_buckets_.add(stat);
  }
}

void ObTabletStream::refresh()
{
  ObTabletStat tablet_stat;
  bool has_retired_stat = false;

  curr_buckets_.refresh(tablet_stat, has_retired_stat);
  latest_buckets_.refresh(tablet_stat, has_retired_stat);
  past_buckets_.refresh(tablet_stat, has_retired_stat);
}

template <uint32_t SIZE>
int ObTabletStream::get_bucket_tablet_stat(
    const ObTabletStatBucket<SIZE> &bucket,
    common::ObIArray<ObTabletStat> &tablet_stats) const
{
  int ret = OB_SUCCESS;
  int64_t idx = bucket.head_idx_;

  for (int64_t i = 0; OB_SUCC(ret) && i < bucket.count(); ++i) {
    int64_t curr_idx = bucket.get_idx(idx);
    if (OB_FAIL(tablet_stats.push_back(bucket.units_[curr_idx]))) {
      LOG_WARN("failed to add tablet stat", K(ret), K(idx));
    }
    ++idx;
  }
  return ret;
}

int ObTabletStream::get_all_tablet_stat(common::ObIArray<ObTabletStat> &tablet_stats) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(get_bucket_tablet_stat(curr_buckets_, tablet_stats))) {
    LOG_WARN("failed to get bucket tablet stat in past bucket", K(ret));
  } else if (OB_FAIL(get_bucket_tablet_stat(latest_buckets_, tablet_stats))) {
    LOG_WARN("failed to get bucket tablet stat in latest bucket", K(ret));
  } else if (OB_FAIL(get_bucket_tablet_stat(past_buckets_, tablet_stats))) {
    LOG_WARN("failed to get bucket tablet stat in curr bucket", K(ret));
  }
  return ret;
}


/************************************* ObTabletStreamPool *************************************/
ObTabletStreamPool::ObTabletStreamPool()
  : dynamic_allocator_(MTL_ID()),
    free_list_allocator_("FreeTbltStream"),
    free_list_(),
    max_free_list_num_(0),
    max_dynamic_node_num_(0),
    allocated_dynamic_num_(0),
    is_inited_(false)
{
}

ObTabletStreamPool::~ObTabletStreamPool()
{
  destroy();
}

void ObTabletStreamPool::destroy()
{
  is_inited_ = false;
  ObTabletStreamNode *node = nullptr;

  while (OB_SUCCESS == free_list_.pop(node)) {
    if (OB_NOT_NULL(node)) {
      node->~ObTabletStreamNode();
      node = nullptr;
    }
  }
  dynamic_allocator_.reset();
  free_list_.destroy();
  free_list_allocator_.reset();
}

int ObTabletStreamPool::init(
    const int64_t max_free_list_num,
    const int64_t max_dynamic_node_num)
{
  int ret = OB_SUCCESS;
  const char *LABEL = "IncTbltStream";
  ObTabletStreamNode *buf = nullptr;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTabletStreamPool has been inited", K(ret));
  } else if (max_free_list_num <= 0 || max_dynamic_node_num < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid argument", K(ret), K(max_free_list_num), K(max_dynamic_node_num));
  } else if (OB_FAIL(dynamic_allocator_.init(ObMallocAllocator::get_instance(), OB_MALLOC_NORMAL_BLOCK_SIZE))) {
    LOG_WARN("failed to init fifo allocator", K(ret));
  } else if (OB_FAIL(free_list_.init(max_free_list_num, &free_list_allocator_))) {
    LOG_WARN("failed to init free list", K(ret), K(max_free_list_num));
  } else if (OB_ISNULL(buf = static_cast<ObTabletStreamNode*>(free_list_allocator_.alloc(sizeof(ObTabletStreamNode) * max_free_list_num)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate memory for stream node in free list", K(ret), K(max_free_list_num));
  } else {
    dynamic_allocator_.set_label(LABEL);
    ObTabletStreamNode *node = nullptr;
    for (int64_t i = 0; OB_SUCC(ret) && i < max_free_list_num; ++i) {
      node = new (buf + i) ObTabletStreamNode(FIXED_ALLOC);
      if (OB_FAIL(free_list_.push(node))) {
        LOG_WARN("failed to push node to free list", K(ret));
      }
    }

    if (OB_FAIL(ret)) {
      destroy();
    } else {
      max_free_list_num_ = max_free_list_num;
      max_dynamic_node_num_ = max_dynamic_node_num;
      is_inited_ = true;
    }
  }
  return ret;
}

int ObTabletStreamPool::alloc(ObTabletStreamNode *&free_node)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletStreamPool not inited", K(ret));
  } else if (OB_NOT_NULL(free_node)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid argument", K(ret), K(free_node));
  } else if (OB_FAIL(free_list_.pop(free_node))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("failed to pop free node from free list", K(ret));
    } else {
      ret = OB_SUCCESS;
    }
  }

  if (OB_FAIL(ret)) {
  } else if (NULL == free_node) {
    if (allocated_dynamic_num_ >= max_dynamic_node_num_) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("the number of allocated dynamic node has reached MAX", K(ret), K(max_dynamic_node_num_), K(allocated_dynamic_num_));
    } else if (OB_ISNULL(buf = dynamic_allocator_.alloc(sizeof(ObTabletStreamNode)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory for free node", K(ret));
    } else {
      free_node = new (buf) ObTabletStreamNode(DYNAMIC_ALLOC);
      ++allocated_dynamic_num_;
    }
  }
  return ret;
}

void ObTabletStreamPool::free(ObTabletStreamNode *node)
{
  if (OB_NOT_NULL(node)) {
    int tmp_ret = OB_SUCCESS;
    if (IS_NOT_INIT) {
      tmp_ret = OB_NOT_INIT;
      LOG_ERROR("[MEMORY LEAK] ObTabletStreamPool is not inited, cannot free this node!!!", K(tmp_ret), KPC(node));
    } else if (DYNAMIC_ALLOC == node->flag_) {
      node->~ObTabletStreamNode();
      dynamic_allocator_.free(node);
      --allocated_dynamic_num_;
    } else {
      node->reset();
      OB_ASSERT(OB_SUCCESS == free_list_.push(node));
    }
  }
}


/************************************* ObTenantTabletStatMgr *************************************/
ObTenantTabletStatMgr::ObTenantTabletStatMgr()
  : report_stat_task_(*this),
    stream_pool_(),
    stream_map_(),
    lru_list_(),
    bucket_lock_(),
    report_queue_(),
    report_cursor_(0),
    pending_cursor_(0),
    report_tg_id_(0),
    is_inited_(false)
{
}

ObTenantTabletStatMgr::~ObTenantTabletStatMgr()
{
  destroy();
}

int ObTenantTabletStatMgr::init()
{
  int ret = OB_SUCCESS;
  const bool repeat = true;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTenantTabletStatMgr init twice", K(ret));
  } else if (OB_FAIL(stream_pool_.init(DEFAULT_MAX_FREE_STREAM_CNT, DEFAULT_UP_LIMIT_STREAM_CNT))) {
    LOG_WARN("failed to init tablet stream pool", K(ret));
  } else if (OB_FAIL(stream_map_.create(DEFAULT_BUCKET_NUM, "TabletStats"))) {
    LOG_WARN("failed to create TabletStats", K(ret));
  } else if (OB_FAIL(bucket_lock_.init(DEFAULT_BUCKET_NUM))) {
    LOG_WARN("failed to init bucket lock", K(ret));
  } else if (OB_FAIL(TG_CREATE_TENANT(lib::TGDefIDs::TabletStatRpt, report_tg_id_))) {
    LOG_WARN("failed to create TabletStatRpt thread", K(ret));
  } else if (OB_FAIL(TG_START(report_tg_id_))) {
    LOG_WARN("failed to start stat TabletStatRpt thread", K(ret));
  } else if (OB_FAIL(TG_SCHEDULE(report_tg_id_, report_stat_task_, TABLET_STAT_PROCESS_INTERVAL, repeat))) {
    LOG_WARN("failed to schedule tablet stat update task", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObTenantTabletStatMgr::mtl_init(ObTenantTabletStatMgr* &tablet_stat_mgr)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(tablet_stat_mgr->init())) {
    LOG_WARN("failed to init tablet stat mgr", K(ret), K(MTL_ID()));
  } else {
    LOG_INFO("success to init ObTenantTabletStatMgr", K(MTL_ID()));
  }
  return ret;
}

void ObTenantTabletStatMgr::wait()
{
  TG_WAIT(report_tg_id_);
}

void ObTenantTabletStatMgr::stop()
{
  TG_STOP(report_tg_id_);
}

void ObTenantTabletStatMgr::destroy()
{
  stop();
  wait();
  TG_DESTROY(report_tg_id_);
  {
    ObBucketWLockAllGuard lock_guard(bucket_lock_);
    stream_map_.destroy();
    stream_pool_.destroy();
    lru_list_.reset();
    report_cursor_ = 0;
    pending_cursor_ = 0;
    report_tg_id_ = 0;
    is_inited_ = false;
  }
  bucket_lock_.destroy();
  FLOG_INFO("ObTenantTabletStatMgr destroyed!");
}

int ObTenantTabletStatMgr::report_stat(const ObTabletStat &stat)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTabletStatMgr not inited", K(ret));
  } else if (!stat.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid arguments", K(ret), K(stat));
  } else {
    int64_t retry_cnt = 0;
    while (retry_cnt < MAX_REPORT_RETRY_CNT) {
      uint64_t pending_cur = ATOMIC_LOAD(&pending_cursor_);
      uint64_t report_cur = ATOMIC_LOAD(&report_cursor_);
      if (pending_cur - report_cur + 1 == DEFAULT_MAX_PENDING_CNT) { // full queue
        LOG_INFO("report_queue is full, wait to process", K(report_cur), K(pending_cur), K(stat));
        break;
      } else if (pending_cur != ATOMIC_CAS(&pending_cursor_, pending_cur, pending_cur + 1)) {
        ++retry_cnt;
      } else {
        report_queue_[pending_cur % DEFAULT_MAX_PENDING_CNT] = stat; // allow dirty write
        break;
      }
    }
    if (retry_cnt == MAX_REPORT_RETRY_CNT) {
      // pending cursor has been moved in other thread, ignore this tablet_stat
      LOG_INFO("pending cursor has beed moved in other thread, ignore current stat", K(stat));
    }
  }
  return ret;
}

int ObTenantTabletStatMgr::get_latest_tablet_stat(
    const share::ObLSID &ls_id,
    const common::ObTabletID &tablet_id,
    ObTabletStat &tablet_stat)
{
  int ret = OB_SUCCESS;
  tablet_stat.reset();
  tablet_stat.ls_id_ = ls_id.id();
  tablet_stat.tablet_id_ = tablet_id.id();
  const ObTabletStatKey key(ls_id, tablet_id);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTabletStatMgr not inited", K(ret));
  } else if (OB_UNLIKELY(!key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid arguments", K(ret), K(ls_id), K(tablet_id));
  } else {
    ObTabletStreamNode *stream_node = nullptr;
    ObBucketHashRLockGuard lock_guard(bucket_lock_, key.hash());
    if (OB_FAIL(stream_map_.get_refactored(key, stream_node))) {
      if (OB_HASH_NOT_EXIST != ret) {
        LOG_WARN("failed to get history stat", K(ret), K(key));
      }
    } else {
      stream_node->stream_.get_latest_stat(tablet_stat);
    }
  }
  return ret;
}

int ObTenantTabletStatMgr::get_history_tablet_stats(
    const share::ObLSID &ls_id,
    const common::ObTabletID &tablet_id,
    common::ObIArray<ObTabletStat> &tablet_stats)
{
  int ret = OB_SUCCESS;
  const ObTabletStatKey key(ls_id, tablet_id);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantTabletStatMgr not inited", K(ret));
  } else if (OB_UNLIKELY(!key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid arguments", K(ret), K(ls_id), K(tablet_id));
  } else {
    ObTabletStreamNode *stream_node = nullptr;
    ObBucketHashRLockGuard lock_guard(bucket_lock_, key.hash());
    if (OB_FAIL(stream_map_.get_refactored(key, stream_node))) {
      if (OB_HASH_NOT_EXIST != ret) {
        LOG_WARN("failed to get history stat", K(ret), K(key));
      }
    } else if (OB_FAIL(stream_node->stream_.get_all_tablet_stat(tablet_stats))) {
      LOG_WARN("failed to get all tablet stat", K(ret), K(key));
    }
  }
  return ret;
}

int ObTenantTabletStatMgr::update_tablet_stream(const ObTabletStat &report_stat)
{
  int ret = OB_SUCCESS;
  ObTabletStreamNode *stream_node = nullptr;
  ObTabletStatKey key(report_stat.ls_id_, report_stat.tablet_id_);
  {
    ObBucketHashRLockGuard lock_guard(bucket_lock_, key.hash());
    ret = stream_map_.get_refactored(key, stream_node);
  }

  if (OB_SUCC(ret)) {
  } else if (OB_HASH_NOT_EXIST == ret) {
    ret = OB_SUCCESS;
    if (OB_FAIL(fetch_node(stream_node))) {
      LOG_WARN("failed to fetch node from stream pool", K(ret), K(report_stat));
    } else {
      ObBucketHashWLockGuard lock_guard(bucket_lock_, key.hash());
      if (OB_FAIL(stream_map_.set_refactored(key, stream_node))) {
        LOG_WARN("failed to update stat map", K(ret), K(report_stat));
      }
    }
  } else {
    LOG_WARN("failed to get stream node from stream map", K(ret), K(key));
  }

  if (OB_SUCC(ret)) {
    if (OB_ISNULL(stream_node)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("stream node is unexpected null", K(ret), K(report_stat));
    } else if (!lru_list_.move_to_first(stream_node)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to add node to lru list", K(ret), K(stream_node));
    } else {
      ObBucketHashWLockGuard lock_guard(bucket_lock_, key.hash());
      stream_node->stream_.add_stat(report_stat);
    }
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(stream_node)) {
    stream_pool_.free(stream_node);
    stream_node = nullptr;
  }
  return ret;
}

int ObTenantTabletStatMgr::fetch_node(ObTabletStreamNode *&node)
{
  int ret = OB_SUCCESS;
  node = nullptr;
  if (OB_FAIL(stream_pool_.alloc(node))) {
    if (OB_SIZE_OVERFLOW == ret) {
      if (lru_list_.is_empty()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("lru list is unexpected null", K(ret));
      } else {
        ret = OB_SUCCESS;
        ObTabletStatKey old_key = lru_list_.get_last()->stream_.get_tablet_stat_key();
        ObBucketHashWLockGuard lock_guard(bucket_lock_, old_key.hash());
        if (OB_FAIL(stream_map_.erase_refactored(old_key))) {
          LOG_WARN("failed to erase tablet stat stream", K(ret), K(old_key));
        } else {
          node = lru_list_.remove_last();
          node->stream_.reset();
        }
      }
    } else {
      LOG_WARN("failed to get free node from stream pool", K(ret));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(node)) {
  } else if (!lru_list_.add_first(node)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to add node to lru list", K(ret), KPC(node));
  }
  return ret;
}

void ObTenantTabletStatMgr::dump_tablet_stat_status()
{
  if (REACH_TENANT_TIME_INTERVAL(DUMP_TABLET_STAT_INTERVAL)) {
    uint64_t start_idx = report_cursor_; // it's OK to dirty read
    uint64_t end_idx = pending_cursor_;
    int64_t map_size = stream_map_.size();
    int64_t stream_node_cnt = stream_pool_.get_allocated_num();

    LOG_INFO("dump_tablet_stat_status",
        "queue_cnt", end_idx - start_idx, K(start_idx), K(end_idx),
        "map_size", map_size,
        "stream_node_cnt", stream_node_cnt);
  }
}

void ObTenantTabletStatMgr::process_stats()
{
  int tmp_ret = OB_SUCCESS;
  uint64_t start_idx = ATOMIC_LOAD(&report_cursor_);
  const uint64_t end_idx = ATOMIC_LOAD(&pending_cursor_);

  if (start_idx == end_idx) { // empty queue
  } else {
    for (uint64_t i = start_idx; i < end_idx; ++i) {
      const ObTabletStat &cur_stat = report_queue_[i % DEFAULT_MAX_PENDING_CNT];
      if (!cur_stat.is_valid()) {
        // allow dirty read
      } else if (OB_TMP_FAIL(update_tablet_stream(cur_stat))) {
        LOG_WARN("failed to update tablet stat", K(tmp_ret), K(cur_stat));
      }
    }
    ATOMIC_STORE(&report_cursor_, end_idx);
  }
}

void ObTenantTabletStatMgr::refresh_all(const int64_t step)
{
  TabletStreamMap::iterator iter = stream_map_.begin();
  for ( ; iter != stream_map_.end(); ++iter) {
    for (int64_t i = 0; i < step; ++i) {
      iter->second->stream_.refresh();
    }
  }
}

void ObTenantTabletStatMgr::TabletStatUpdater::runTimerTask()
{
  mgr_.dump_tablet_stat_status();
  mgr_.process_stats();

  int64_t interval_step = 0;
  if (CHECK_SCHEDULE_TIME_INTERVAL(CHECK_INTERVAL, interval_step)) {
    if (OB_UNLIKELY(interval_step > 1)) {
      LOG_WARN("tablet streams not refresh too long", K(interval_step));
    }
    mgr_.refresh_all(interval_step);
    FLOG_INFO("TenantTabletStatMgr refresh all tablet stream", K(MTL_ID()), K(interval_step));
  }
}
