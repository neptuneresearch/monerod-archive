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
// ** Patched with MonerodArchive v7 by Neptune Research
// ** SPDX-License-Identifier: BSD-3-Clause
// ** Changed code appears below.

bool Blockchain::init(BlockchainDB* db, const network_type nettype, bool offline, const cryptonote::test_options *test_options, difficulty_type fixed_difficulty)
{
  LOG_PRINT_L3("Blockchain::" << __func__);
  CRITICAL_REGION_LOCAL(m_tx_pool);
  CRITICAL_REGION_LOCAL1(m_blockchain_lock);

  if (db == nullptr)
  {
    LOG_ERROR("Attempted to init Blockchain with null DB");
    return false;
  }
  if (!db->is_open())
  {
    LOG_ERROR("Attempted to init Blockchain with unopened DB");
    delete db;
    return false;
  }

  m_db = db;

  m_nettype = test_options != NULL ? FAKECHAIN : nettype;
  m_offline = offline;
  m_fixed_difficulty = fixed_difficulty;
  if (m_hardfork == nullptr)
  {
    if (m_nettype ==  FAKECHAIN || m_nettype == STAGENET)
      m_hardfork = new HardFork(*db, 1, 0);
    else if (m_nettype == TESTNET)
      m_hardfork = new HardFork(*db, 1, testnet_hard_fork_version_1_till);
    else
      m_hardfork = new HardFork(*db, 1, mainnet_hard_fork_version_1_till);
  }
  if (m_nettype == FAKECHAIN)
  {
    for (size_t n = 0; test_options->hard_forks[n].first; ++n)
      m_hardfork->add_fork(test_options->hard_forks[n].first, test_options->hard_forks[n].second, 0, n + 1);
  }
  else if (m_nettype == TESTNET)
  {
    for (size_t n = 0; n < sizeof(testnet_hard_forks) / sizeof(testnet_hard_forks[0]); ++n)
      m_hardfork->add_fork(testnet_hard_forks[n].version, testnet_hard_forks[n].height, testnet_hard_forks[n].threshold, testnet_hard_forks[n].time);
  }
  else if (m_nettype == STAGENET)
  {
    for (size_t n = 0; n < sizeof(stagenet_hard_forks) / sizeof(stagenet_hard_forks[0]); ++n)
      m_hardfork->add_fork(stagenet_hard_forks[n].version, stagenet_hard_forks[n].height, stagenet_hard_forks[n].threshold, stagenet_hard_forks[n].time);
  }
  else
  {
    for (size_t n = 0; n < sizeof(mainnet_hard_forks) / sizeof(mainnet_hard_forks[0]); ++n)
      m_hardfork->add_fork(mainnet_hard_forks[n].version, mainnet_hard_forks[n].height, mainnet_hard_forks[n].threshold, mainnet_hard_forks[n].time);
  }
  m_hardfork->init();

  m_db->set_hard_fork(m_hardfork);

  // if the blockchain is new, add the genesis block
  // this feels kinda kludgy to do it this way, but can be looked at later.
  // TODO: add function to create and store genesis block,
  //       taking testnet into account
  if(!m_db->height())
  {
    MINFO("Blockchain not loaded, generating genesis block.");
    block bl = boost::value_initialized<block>();
    block_verification_context bvc = boost::value_initialized<block_verification_context>();
    generate_genesis_block(bl, get_config(m_nettype).GENESIS_TX, get_config(m_nettype).GENESIS_NONCE);
    // <MonerodArchive (IsNodeSynced?3)>
    add_new_block(bl, bvc, std::make_pair(0, 0));
    // </MonerodArchive>
    CHECK_AND_ASSERT_MES(!bvc.m_verifivation_failed, false, "Failed to add genesis block to blockchain");
  }
  // TODO: if blockchain load successful, verify blockchain against both
  //       hard-coded and runtime-loaded (and enforced) checkpoints.
  else
  {
  }

  if (m_nettype != FAKECHAIN)
  {
    // ensure we fixup anything we found and fix in the future
    m_db->fixup();
  }

  m_db->block_txn_start(true);
  // check how far behind we are
  uint64_t top_block_timestamp = m_db->get_top_block_timestamp();
  uint64_t timestamp_diff = time(NULL) - top_block_timestamp;

  // genesis block has no timestamp, could probably change it to have timestamp of 1341378000...
  if(!top_block_timestamp)
    timestamp_diff = time(NULL) - 1341378000;

  // create general purpose async service queue

  m_async_work_idle = std::unique_ptr < boost::asio::io_service::work > (new boost::asio::io_service::work(m_async_service));
  // we only need 1
  m_async_pool.create_thread(boost::bind(&boost::asio::io_service::run, &m_async_service));

#if defined(PER_BLOCK_CHECKPOINT)
  if (m_nettype != FAKECHAIN)
    load_compiled_in_block_hashes();
#endif

  MINFO("Blockchain initialized. last block: " << m_db->height() - 1 << ", " << epee::misc_utils::get_time_interval_string(timestamp_diff) << " time ago, current difficulty: " << get_difficulty_for_next_block());
  m_db->block_txn_stop();

  uint64_t num_popped_blocks = 0;
  while (!m_db->is_read_only())
  {
    const uint64_t top_height = m_db->height() - 1;
    const crypto::hash top_id = m_db->top_block_hash();
    const block top_block = m_db->get_top_block();
    const uint8_t ideal_hf_version = get_ideal_hard_fork_version(top_height);
    if (ideal_hf_version <= 1 || ideal_hf_version == top_block.major_version)
    {
      if (num_popped_blocks > 0)
        MGINFO("Initial popping done, top block: " << top_id << ", top height: " << top_height << ", block version: " << (uint64_t)top_block.major_version);
      break;
    }
    else
    {
      if (num_popped_blocks == 0)
        MGINFO("Current top block " << top_id << " at height " << top_height << " has version " << (uint64_t)top_block.major_version << " which disagrees with the ideal version " << (uint64_t)ideal_hf_version);
      if (num_popped_blocks % 100 == 0)
        MGINFO("Popping blocks... " << top_height);
      ++num_popped_blocks;
      block popped_block;
      std::vector<transaction> popped_txs;
      try
      {
        m_db->pop_block(popped_block, popped_txs);
      }
      // anything that could cause this to throw is likely catastrophic,
      // so we re-throw
      catch (const std::exception& e)
      {
        MERROR("Error popping block from blockchain: " << e.what());
        throw;
      }
      catch (...)
      {
        MERROR("Error popping block from blockchain, throwing!");
        throw;
      }
    }
  }
  if (num_popped_blocks > 0)
  {
    m_timestamps_and_difficulties_height = 0;
    m_hardfork->reorganize_from_chain_height(get_current_blockchain_height());
    m_tx_pool.on_blockchain_dec(m_db->height()-1, get_tail_id());
  }

  update_next_cumulative_size_limit();
  return true;
}

//------------------------------------------------------------------
bool Blockchain::reset_and_set_genesis_block(const block& b)
{
  LOG_PRINT_L3("Blockchain::" << __func__);
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  m_timestamps_and_difficulties_height = 0;
  m_alternative_chains.clear();
  invalidate_block_template_cache();
  m_db->reset();
  m_hardfork->init();

  block_verification_context bvc = boost::value_initialized<block_verification_context>();
  // <MonerodArchive (IsNodeSynced?4)>
  add_new_block(b, bvc, std::make_pair(0, 0));
  // <MonerodArchive>
  update_next_cumulative_size_limit();
  return bvc.m_added_to_main_chain && !bvc.m_verifivation_failed;
}

//------------------------------------------------------------------
/*
  MonerodArchive: Monero 0.12.3.0 Blockchain::add_new_block patch
*/
bool Blockchain::add_new_block(const block& bl_, block_verification_context& bvc, std::pair<uint64_t,uint64_t> archive_sync_state)
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
    archive_block(bl_archive, true, archive_sync_state);
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
    archive_block(bl_archive, false, archive_sync_state);
  }
  // </MonerodArchive (Main Block)>

  m_db->block_txn_stop();
  return handle_block_to_main_chain(bl, id, bvc);
}
//------------------------------------------------------------------
/*
  <MonerodArchive>
*/
void Blockchain::archive_block(block& b, bool is_alt_block, std::pair<uint64_t,uint64_t> archive_sync_state)
{
  // ## read archive configuration
  std::string filename_archive = archive_output_filename();
  std::string output_field_delimiter = "\t";
  uint64_t output_format_version = 7;

  // ## alt_chain_info
  std::pair<uint64_t,std::string> altchaininfo = archive_alt_chain_info();
  uint64_t altchaininfo_length = altchaininfo.first;
  std::string altchaininfo_json = altchaininfo.second;

  // ## sync state
  uint64_t archive_current_height = archive_sync_state.first;
  uint64_t archive_target_height = archive_sync_state.second;
  bool is_node_synced = (archive_current_height >= archive_target_height);
  
  // ## get data from block
  // block height: miner_tx => txin_v transaction.vin => txin_v[0] => txin_v.txin_gen => txin_gen.height
  size_t block_height = boost::get<txin_gen>(b.miner_tx.vin[0]).height;
  // block timestamp (MRT)
  uint64_t block_timestamp = b.timestamp;
  // node_timestamp (NRT)
  uint64_t node_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

  // ## OUTPUT - Daemon console
  std::stringstream patch_log;
  patch_log << "Block Archive"
    << (is_alt_block ? " ALT " : " MAIN")
    << " H=" << block_height 
    << " MRT=" << block_timestamp 
    << " NRT=" << node_timestamp
    << " n_alt_chains=" << altchaininfo_length
    << (is_node_synced ? " FULL" : " SYNC")
    << " NCH=" << archive_current_height
    << " NTH=" << archive_target_height;
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
  archive_line << "" << output_format_version // 1
    << output_field_delimiter
    << node_timestamp   // 2
    << output_field_delimiter
    << (is_alt_block ? "1" : "0") // 3
    << output_field_delimiter;
  archive_line << (block_json_success ? block_json_buf.str() : "{}"); // 4
  archive_line << output_field_delimiter
    << altchaininfo_length  // 5
    << output_field_delimiter
    << altchaininfo_json  // 6
    << output_field_delimiter
    << (is_node_synced ? "1" : "0") // 7
    << output_field_delimiter
    << archive_current_height // 8
    << output_field_delimiter
    << archive_target_height  // 9
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