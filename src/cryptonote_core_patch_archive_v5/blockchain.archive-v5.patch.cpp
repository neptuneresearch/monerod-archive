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
// ** Patched with MonerodArchive v5 by Neptune Research
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

#include <ctime> // MonerodArchive Dependency #1

/*
   MonerodArchive: ... original Monero code omitted here ...
*/

/*
  MonerodArchive: Monero 0.12.1.0 Blockchain::add_new_block patch
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

  // <MonerodArchive (All Blocks) patch>
  block& bl_archive = const_cast<block&>(bl_);
  // <MonerodArchive (All Blocks) patch>

  //check that block refers to chain tail
  if(!(bl.prev_id == get_tail_id()))
  {
    // <MonerodArchive (Alt Block) patch>
    archive_block(bl_archive, true);
    // </MonerodArchive (Alt Block) patch>

    //chain switching or wrong block
    bvc.m_added_to_main_chain = false;
    m_db->block_txn_stop();
    bool r = handle_alternative_block(bl, id, bvc);
    m_blocks_txs_check.clear();
    return r;
    //never relay alternative blocks
  }
  // <MonerodArchive (Main Block) patch>
  else
  {
    archive_block(bl_archive, false);
  }
  // </MonerodArchive (Main Block) patch>

  m_db->block_txn_stop();
  return handle_block_to_main_chain(bl, id, bvc);
}
//------------------------------------------------------------------
/*
  <MonerodArchive>
*/
void Blockchain::archive_block(block& b, bool is_alt_block) {
  // ## read config
  std::string archive_path = archive_output_directory();
  // ## enable v6 file append
  #ifdef WIN32
    // WIN32 not compatible with v6 method because epee::file_io_utils::append_string_to_file does not support win32.
    // WIN32 case will use v5 method with filename_gen_block and filename_gen_altchain to create new files.
    bool append = false;
  #else
    // v6 append supported
    bool append = true;
  #endif

  // ## create altchaininfo
  std::string altchaininfo = archive_alt_chain_info();
  bool altchaininfo_valid = (altchaininfo.length() > 0);

  // ## get some data
  // block height: miner_tx => txin_v transaction.vin => txin_v[0] => txin_v.txin_gen => txin_gen.height
  size_t block_height = boost::get<txin_gen>(b.miner_tx.vin[0]).height;
  // block timestamp (MRT)
  uint64_t block_timestamp = b.timestamp;
  // node_timestamp (NRT)
  uint64_t node_timestamp = 0;

  // ## filename()
  if(append)
  {
    // ## filename_v6(archive_path)
    std::string filename_blocks;
    std::string filename_altchaininfo;
    filename_blocks << archive_path << "blocks.json";
    filename_altchaininfo << archive_path << "altchaininfo.log"
  }
  else
  {
    // ## filename_gen_2(archive_path)
    // make a random number
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    long current_time = (tv.tv_sec * 1000 + tv.tv_usec / 1000);
    srand(current_time);
    int random_number = std::rand();
    // read block header
    uint32_t block_nonce = b.nonce;
    // v5: make filenames
    std::stringstream block_name_gen, filename_gen_block, filename_gen_altchain;
    block_name_gen << "t" << block_timestamp << "_h" << block_height << "_n" << block_nonce << "_r" << random_number;
    std::string block_name = block_name_gen.str();
    std::string alt_letter = (is_alt_block ? "1" : "0");
    filename_gen_block << archive_path << "block" << alt_letter << "_" << block_name << ".json";
    filename_gen_altchain << archive_path << "altchain_" << block_name << ".log";
  }

  // ## patch_log()
  std::stringstream patch_log;
  patch_log << "Block Archive [Chain=" << (is_alt_block ? "ALT " : "MAIN") << " H=" << block_height << " MRT=" << block_timestamp << " NRT=" << node_timestamp << "]";
  if(altchaininfo_valid) patch_log << "\n" << altchaininfo;
  MCLOG_MAGENTA(el::Level::Info, "global", patch_log.str());

  // ## block_save(block b)
#ifdef WIN32
  // block_json_32
  std::ostringstream block_archive_buf32;
  // #  json flavor
  json_archive<true> block_json(block_archive_buf32, true);
  bool block_json_32_success = ::serialization::serialize(block_json, b);

  // block_save_32
  if(block_json_32_success)
  {
    bool block_save_32_success = epee::file_io_utils::save_string_to_file(filename_gen_block.str(), block_archive_buf32.str());
  }

  // altchain_save_32
  if(altchaininfo_valid)
  {
    bool altchain_save_32_success = epee::file_io_utils::save_string_to_file(filename_gen_altchain.str(), altchaininfo);
  }
#else
    // block_save
    bool success;

    if(append)
    {
      // v6: append to file filename_blocks
      std::ostringstream block_archive_buf;
      // note: second argument is bool indent
      json_archive<true> block_json(block_archive_buf, false);
      bool block_json_success = ::serialization::serialize(block_json, b);
      if(block_json_success)
      {
        bool block_save_success = epee::file_io_utils::append_string_to_file(filename_blocks.str(), block_archive_buf.str());
      }
    }
    else
    {
      // v5: create new file filename_gen_block
      std::ofstream block_archive_buf;
      // note: binary flag here affects eol conversion of text data
      block_archive_buf.open(filename_gen_block.str(), std::ios_base::binary | std::ios_base::out | std::ios_base::trunc);
      // json flavor
      json_archive<true> block_json(block_archive_buf);
      success = ::serialization::serialize(block_json, b);
      block_archive_buf.close();
    }

    // altchain_save
    if(altchaininfo_valid)
    {
      if(append)
      {
        // v6: append to file filename_altchaininfo
        bool altchaininfo_save_success = epee::file_io_utils::append_string_to_file(filename_altchaininfo.str(), altchaininfo);
      }
      else
      {
        // v5: create new file filename_gen_altchain
        std::ofstream altchaininfo_buf;
        altchaininfo_buf.open(filename_gen_altchain.str(), std::ios_base::binary | std::ios_base::out | std::ios_base::trunc);
        altchaininfo_buf << altchaininfo;
        altchaininfo_buf.close();
      }
    }
#endif
}
//-----------------------------------------------------------------------------------------------
std::string Blockchain::archive_alt_chain_info() {
  // rpc_get_info: read height_without_bootstrap
  uint64_t height_without_bootstrap;
  crypto::hash top_hash = get_tail_id(height_without_bootstrap);
  ++height_without_bootstrap; // turn top block height into blockchain height

  // rpc_get_alternate_chains
  std::stringstream info;
  std::list<std::pair<Blockchain::block_extended_info, uint64_t>> chains = get_alternative_chains();

  // validate: return empty string on 0 alt chains
  if(chains.size() == 0) return "";

  // build report
  // note: ";" used instead of "\n" so that the report is only one line
  //  header
  info << boost::lexical_cast<std::string>(chains.size()) << " alternate chains found; ";
  //  each altchain
  for (const auto &i: chains)
  {
    std::string block_hash = epee::string_tools::pod_to_hex(get_block_hash(i.first.bl));
    uint64_t height = i.first.height;
    uint64_t length = i.second;
    uint64_t difficulty = i.first.cumulative_difficulty;
    uint64_t start_height = (height - length + 1);

    info << length << " blocks long, from height " << start_height << " ("
         << (height_without_bootstrap - start_height - 1)
         << " deep), diff " << difficulty << ": " << block_hash << "; ";
  }

  return info.str();
}
//-----------------------------------------------------------------------------------------------
std::string Blockchain::archive_output_directory() {
  // ## USER INPUT
  // # data_dir
  // # - Path must include final slash, Linux "/" or Windows "\\".
  // # - Directory MUST exist, it will not be created.
  std::string data_dir;

#ifdef WIN32
  // #    Windows flavor
  data_dir = "c:\\monerodarchive\\";
#else
  // #    Linux flavor
  data_dir = "/opt/monerodarchive/";
#endif

  return data_dir;
}
/*
  </MonerodArchive>
*/

/*
   MonerodArchive: ... original Monero code omitted here ...
*/