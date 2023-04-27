// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2021, Contributors to the rTorrent project

#include <cstring>
#include <memory>

#include <torrent/exceptions.h>

#include "rpc/rpc_json.h"
#include "rpc/rpc_xml.h"

#include "rpc/rpc_manager.h"

namespace rpc {

RpcManager::RpcManager() {
  m_rpcProcessors[RPCType::XML]  = new RpcXml();
  m_rpcProcessors[RPCType::JSON] = new RpcJson();
}

RpcManager::~RpcManager() {
  delete static_cast<RpcXml*>(m_rpcProcessors[RPCType::XML]);
  delete static_cast<RpcJson*>(m_rpcProcessors[RPCType::JSON]);
}

// fix security hole for untrusted commands
static std::vector<std::string> untrusted_commands =
{
	"execute",
	"execute.capture",
	"execute.capture_nothrow",
	"execute.nothrow",
	"execute.nothrow.bg",
	"execute.raw",
	"execute.raw.bg",
	"execute.raw_nothrow",
	"execute.raw_nothrow.bg",
	"execute.throw",
	"execute.throw.bg",
	"execute2",
	"method.insert",
	"method.redirect",
	"method.set",
	"method.set_key",
	"schedule",
	"schedule2",
	"import",
	"try_import",
	"log.open_file",
	"log.add_output",
	"log.execute",
	"log.vmmap.dump",
	"log.xmlrpc",
	"log.libtorrent",
	"file.append",
// old commands
	"execute_capture",
	"execute_capture_nothrow",
	"execute_nothrow",
	"execute_nothrow_bg",
	"execute_raw",
	"execute_raw_bg",
	"execute_raw_nothrow",
	"execute_raw_nothrow_bg",
	"execute_throw",
	"execute_throw_bg",
	"system.method.insert",
	"system.method.redirect",
	"system.method.set",
	"system.method.set_key",
	"on_insert",
	"on_erase",
	"on_open",
	"on_close",
	"on_start",
	"on_stop",
	"on_hash_queued",
	"on_hash_removed",
	"on_hash_done",
	"on_finished"
};
thread_local bool RpcManager::trustedXmlConnection = true;

bool 
RpcManager::set_trusted_connection( bool enabled )
{
	bool ret = RpcManager::trustedXmlConnection;
	RpcManager::trustedXmlConnection = enabled;
	return(ret);
}

bool 
RpcManager::is_command_enabled( const char* const methodName )
{
	return( trustedXmlConnection ||
		(std::find(untrusted_commands.begin(), untrusted_commands.end(), methodName) == untrusted_commands.end()) );
}

bool
RpcManager::dispatch(RPCType            type,
                     const char*        inBuffer,
                     uint32_t           length,
                     IRpc::res_callback callback,
                     bool               trusted) {
  trustedXmlConnection = trusted;
  switch (type) {
    case RPCType::XML: {
      if (m_rpcProcessors[RPCType::XML]->is_valid()) {
        return m_rpcProcessors[RPCType::XML]->process(
          inBuffer, length, callback, trusted);
      } else {
        const char* response =
          "<?xml version=\"1.0\"?><methodResponse><fault><value><string>XMLRPC "
          "not supported</string></value></fault></methodResponse>";
        return callback(response, strlen(response));
      }
    }
    case RPCType::JSON: {
      if (m_rpcProcessors[RPCType::JSON]->is_valid()) {
        return m_rpcProcessors[RPCType::JSON]->process(
          inBuffer, length, callback, trusted);
      } else {
        const char* response =
          "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32601,\"message\":\"JSON-"
          "RPC not supported\"},\"id\":\"1\"}";
        return callback(response, strlen(response));
      }
    }
    default:
      throw torrent::internal_error("Invalid RPC type.");
  }
  trustedXmlConnection = true;
}

void
RpcManager::initialize(slot_download fun_d,
                       slot_file     fun_f,
                       slot_tracker  fun_t,
                       slot_peer     fun_p) {
  m_initialized = true;

  m_slotFindDownload = fun_d;
  m_slotFindFile     = fun_f;
  m_slotFindTracker  = fun_t;
  m_slotFindPeer     = fun_p;

  m_rpcProcessors[RPCType::XML]->initialize();
  m_rpcProcessors[RPCType::JSON]->initialize();
}

void
RpcManager::cleanup() {
  m_rpcProcessors[RPCType::XML]->cleanup();
  m_rpcProcessors[RPCType::JSON]->cleanup();
}

bool
RpcManager::is_initialized() const {
  return m_initialized;
}

void
RpcManager::insert_command(const char* name,
                           const char* parm,
                           const char* doc) {
  m_rpcProcessors[RPCType::XML]->insert_command(name, parm, doc);
  m_rpcProcessors[RPCType::JSON]->insert_command(name, parm, doc);
}

}