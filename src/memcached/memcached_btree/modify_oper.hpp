// Copyright 2010-2012 RethinkDB, all rights reserved.
#ifndef MEMCACHED_MEMCACHED_BTREE_MODIFY_OPER_HPP_
#define MEMCACHED_MEMCACHED_BTREE_MODIFY_OPER_HPP_

#include "btree/slice.hpp"  // RSI: for SLICE_ALT
#include "containers/scoped.hpp"
#include "memcached/memcached_btree/node.hpp"

#if SLICE_ALT
namespace alt { class alt_buf_parent_t; }
#endif

class btree_slice_t;

#define BTREE_MODIFY_OPER_DUMMY_PROPOSED_CAS 0

/* Stats */
class memcached_modify_oper_t {
protected:
    virtual ~memcached_modify_oper_t() { }

public:
    // run_memcached_modify_oper() calls operate() when it reaches the
    // leaf node.  It modifies the value of (or the existence of
    // `value` in some way.  For example, if value contains a NULL
    // pointer, that means no such key-value pair exists.  Setting the
    // value to NULL would mean to delete the key-value pair (but if
    // you do so make sure to wipe out the blob, too).  The return
    // value is true if the leaf node needs to be updated.
#if SLICE_ALT
    virtual MUST_USE bool operate(alt::alt_buf_parent_t leaf,
                                  scoped_malloc_t<memcached_value_t> *value) = 0;
#else
    virtual MUST_USE bool operate(transaction_t *txn, scoped_malloc_t<memcached_value_t> *value) = 0;
#endif


    virtual MUST_USE int compute_expected_change_count(block_size_t block_size) = 0;
};

class superblock_t;

// Runs a memcached_modify_oper_t.
#if SLICE_ALT
void run_memcached_modify_oper(memcached_modify_oper_t *oper, btree_slice_t *slice,
                               const store_key_t &key, cas_t proposed_cas,
                               exptime_t effective_time, repli_timestamp_t timestamp,
                               superblock_t *superblock);
#else
void run_memcached_modify_oper(memcached_modify_oper_t *oper, btree_slice_t *slice, const store_key_t &key, cas_t proposed_cas, exptime_t effective_time, repli_timestamp_t timestamp,
    transaction_t *txn, superblock_t *superblock);
#endif


#endif // MEMCACHED_MEMCACHED_BTREE_MODIFY_OPER_HPP_
