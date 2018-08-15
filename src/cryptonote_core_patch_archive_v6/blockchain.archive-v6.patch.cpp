// Copyright (c) 2014-2018, The Monero Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Parts of this file are originally copyright (c) 2012-2013 The Cryptonote developers
// 
// ** Patched with MonerodArchive v6 by Neptune Research
// ** SPDX-License-Identifier: BSD-3-Clause

#include <algorithm>
#include <cstdio>
#include <boost/filesystem.hpp>
#include <boost/range/adaptor/reversed.hpp>

#include "include_base_utils.h"
#include "cryptonote_basic/cryptonote_basic_impl.h"
#include "tx_pool.h"
#include "blockchain.h"
#include "blockchain_db/blockchain_db.h"
#include "cryptonote_basic/cryptonote_boost_serialization.h"
#include "cryptonote_config.h"
#include "cryptonote_basic/miner.h"
#include "misc_language.h"
#include "profile_tools.h"
#include "file_io_utils.h"
#include "common/int-util.h"
#include "common/threadpool.h"
#include "common/boost_serialization_helper.h"
#include "warnings.h"
#include "crypto/hash.h"
#include "cryptonote_core.h"
#include "ringct/rctSigs.h"
#include "common/perf_timer.h"
#if defined(PER_BLOCK_CHECKPOINT)
#include "blocks/blocks.h"
#endif

#include <chrono> // MonerodArchive Dependency #1

/*
   MonerodArchive: ... original Monero code omitted here ...
*/

/*
  MonerodArchive: Monero 0.12.3.0 Blockchain::add_new_block patch
*/
bool Blockchain::add_new_block(const block& bl_, block_verification_context& bvc)
{
  LOG_PRINT_L3("Blockchain::" << __func__);
  //copy block here to let modify block.target
  block bl = bl_;
  crypto::hash id = get_block_hash(bl);
  CRITICAL_REGION_LOCAL(m_tx_pool);//to avoid deadlock lets lock tx_pool for whole add/reorganize process
  CRITICAL_REGION_LOCAL1(m_blockchain_lock);
  m_db->block_txn_start(true);
  if(have_block(id))
  {
    LOG_PRINT_L3("block with id = " << id << " already exists");
    bvc.m_already_exists = true;
    m_db->block_txn_stop();
    m_blocks_txs_check.clear();
    return false;
  }

  // <MonerodArchive (All Blocks)>
  block& bl_archive = const_cast<block&>(bl_);
  // <MonerodArchive (All Blocks)>

  //check that block refers to chain tail
  if(!(bl.prev_id == get_tail_id()))
  {
    // <MonerodArchive (Alt Block)>
    archive_block(bl_archive, true);
    // </MonerodArchive (Alt Block)>

    //chain switching or wrong block
    bvc.m_added_to_main_chain = false;
    m_db->block_txn_stop();
    bool r = handle_alternative_block(bl, id, bvc);
    m_blocks_txs_check.clear();
    return r;
    //never relay alternative blocks
  }
  // <MonerodArchive (Main Block)>
  else
  {
    archive_block(bl_archive, false);
  }
  // </MonerodArchive (Main Block)>

  m_db->block_txn_stop();
  return handle_block_to_main_chain(bl, id, bvc);
}
//------------------------------------------------------------------
/*
  <MonerodArchive>
*/
void Blockchain::archive_block(block& b, bool is_alt_block) 
{
  // ## read config
  std::string filename_archive = archive_output_filename();
  std::string output_field_delimiter = "\t";

  // ## get altchaininfo
  std::pair<uint64_t,std::string> altchaininfo = archive_alt_chain_info();
  uint64_t altchaininfo_length = altchaininfo.first;
  std::string altchaininfo_json = altchaininfo.second;
  
  // ## get data from block
  // block height: miner_tx => txin_v transaction.vin => txin_v[0] => txin_v.txin_gen => txin_gen.height
  size_t block_height = boost::get<txin_gen>(b.miner_tx.vin[0]).height;
  // block timestamp (MRT)
  uint64_t block_timestamp = b.timestamp;
  // node_timestamp (NRT)
  uint64_t node_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

  // ## OUTPUT - Daemon console
  std::stringstream patch_log;
  patch_log << "Block Archive "
    << (is_alt_block ? "ALT " : "MAIN") 
    << " H=" << block_height 
    << " MRT=" << block_timestamp 
    << " NRT=" << node_timestamp
    << " n_alt_chains=" << altchaininfo_length;
  MCLOG_MAGENTA(el::Level::Info, "global", patch_log.str());

  // ## OUTPUT - Filesystem recording
  // ### serialize block
  std::ostringstream block_json_buf;
  // note: second argument to json_archive() is bool indent
  json_archive<true> block_json(block_json_buf, false);
  bool block_json_success = ::serialization::serialize(block_json, b);

  // ### make archive line
  std::stringstream archive_line;
  // note: string << int required for int to string conversion
  archive_line << "" << node_timestamp 
    << output_field_delimiter
    << (is_alt_block ? "1" : "0")
    << output_field_delimiter;
  archive_line << (block_json_success ? block_json_buf.str() : "{}");
  archive_line << output_field_delimiter
    << altchaininfo_length
    << output_field_delimiter
    << altchaininfo_json
    << "\n";
  bool save_success = epee::file_io_utils::append_string_to_file(filename_archive, archive_line.str());
}
//-----------------------------------------------------------------------------------------------
std::pair<uint64_t,std::string> Blockchain::archive_alt_chain_info() 
{
  // rpc_get_info: read height_without_bootstrap
  uint64_t height_without_bootstrap;
  get_tail_id(height_without_bootstrap);
  ++height_without_bootstrap; // turn top block height into blockchain height

  // rpc_get_alternate_chains
  std::list<std::pair<Blockchain::block_extended_info, std::vector<crypto::hash>>> chains = get_alternative_chains();
  uint64_t altchains_length = boost::lexical_cast<uint64_t>(chains.size());

  // serialize altchains
  std::stringstream altchains_json;
  if(altchains_length > 0)
  {
    //  root array start
    altchains_json << "[";

    //  each altchain
    bool firstchain = false;
    for (const auto &chain: chains)
    {
      uint64_t length = chain.second.size();
      uint64_t start_height = (chain.first.height - length + 1);
      uint64_t deep = (height_without_bootstrap - start_height - 1);
      std::string block_hash = epee::string_tools::pod_to_hex(get_block_hash(chain.first.bl));

      // n > 1 : add array delimiter
      if(!firstchain)
      {
        firstchain = true;
      }
      else
      {
        altchains_json << ",";
      }
      // serialize chain
      altchains_json << "{"
                          << "\"length\"" << ":" << length << ","
                          << "\"height\"" << ":" << start_height << ","
                          << "\"deep\""   << ":" << deep << ","
                          << "\"diff\""   << ":" << chain.first.cumulative_difficulty << ","
                          << "\"hash\""   << ":" << "\"" << block_hash << "\""
                      << "}";
    }

    //  root array end
    altchains_json << "]";
  }
  else
  {
    // root array empty
    altchains_json << "[]";
  }

  return std::make_pair(altchains_length, altchains_json.str());
}
//-----------------------------------------------------------------------------------------------
std::string Blockchain::archive_output_filename() 
{
  // ## USER INPUT
  // # output_filename
  // # - Directory MUST exist, it will not be created.
  std::string output_filename;

  // # Linux flavor
  output_filename = "/opt/monerodarchive/archive.log";

  return output_filename;
}
/*
  </MonerodArchive>
*/

/*
   MonerodArchive: ... original Monero code omitted here ...
*/