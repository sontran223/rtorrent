// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2005-2011, Jari Sundell <jaris@ifi.uio.no>

#include <algorithm>

#include <curl/multi.h>
#include <torrent/exceptions.h>

#include "core/curl_get.h"
#include "core/curl_socket.h"
#include "core/curl_stack.h"

namespace core {

CurlStack::CurlStack()
  : m_handle((void*)curl_multi_init()) {
  m_taskTimeout.slot() = [this] { receive_timeout(); };

  curl_multi_setopt((CURLM*)m_handle, CURLMOPT_TIMERDATA, this);
  curl_multi_setopt(
    (CURLM*)m_handle, CURLMOPT_TIMERFUNCTION, &CurlStack::set_timeout);
  curl_multi_setopt((CURLM*)m_handle, CURLMOPT_SOCKETDATA, this);
  curl_multi_setopt(
    (CURLM*)m_handle, CURLMOPT_SOCKETFUNCTION, &CurlSocket::receive_socket);
}

CurlStack::~CurlStack() {
  while (!empty())
    front()->close();

  curl_multi_cleanup((CURLM*)m_handle);
  priority_queue_erase(&taskScheduler, &m_taskTimeout);
}

CurlGet*
CurlStack::new_object() {
  return new CurlGet(this);
}

CurlSocket*
CurlStack::new_socket(int fd) {
  CurlSocket* socket = new CurlSocket(fd, this);
  curl_multi_assign((CURLM*)m_handle, fd, socket);
  return socket;
}

void
CurlStack::receive_action(CurlSocket* socket, int events) {
  CURLMcode code;

  do {
    int count;
    code = curl_multi_socket_action(
      (CURLM*)m_handle,
      socket != nullptr ? socket->file_descriptor() : CURL_SOCKET_TIMEOUT,
      events,
      &count);

    if (code > 0)
      throw torrent::internal_error("Error calling curl_multi_socket_action.");

    // Socket might be removed when cleaning handles below, future
    // calls should not use it.
    socket = nullptr;
    events = 0;

    if ((unsigned int)count != size()) {
      while (process_done_handle())
        ; // Do nothing.

      if (empty())
        priority_queue_erase(&taskScheduler, &m_taskTimeout);
    }

  } while (code == CURLM_CALL_MULTI_PERFORM);
}

bool
CurlStack::process_done_handle() {
  int      remaining_msgs = 0;
  CURLMsg* msg = curl_multi_info_read((CURLM*)m_handle, &remaining_msgs);

  if (msg == nullptr)
    return false;

  if (msg->msg != CURLMSG_DONE)
    throw torrent::internal_error(
      "CurlStack::receive_action() msg->msg != CURLMSG_DONE.");

  if (msg->data.result == CURLE_COULDNT_RESOLVE_HOST) {
    iterator itr = std::find_if(begin(), end(), [msg](CurlGet* g) {
      return msg->easy_handle == g->handle();
    });

    if (itr == end())
      throw torrent::internal_error(
        "Could not find CurlGet when calling CurlStack::receive_action.");

    if (!(*itr)->is_using_ipv6()) {
      (*itr)->retry_ipv6();

      if (curl_multi_add_handle((CURLM*)m_handle, (*itr)->handle()) > 0)
        throw torrent::internal_error("Error calling curl_multi_add_handle.");
    }

  } else {
    transfer_done(msg->easy_handle,
                  msg->data.result == CURLE_OK
                    ? nullptr
                    : curl_easy_strerror(msg->data.result));
  }

  return remaining_msgs != 0;
}

void
CurlStack::transfer_done(void* handle, const char* msg) {
  iterator itr = std::find_if(
    begin(), end(), [handle](CurlGet* g) { return handle == g->handle(); });

  if (itr == end())
    throw torrent::internal_error(
      "Could not find CurlGet with the right easy_handle.");

  if (msg == nullptr)
    (*itr)->trigger_done();
  else
    (*itr)->trigger_failed(msg);
}

void
CurlStack::receive_timeout() {
  receive_action(nullptr, 0);

  // Sometimes libcurl forgets to reset the timeout. Try to poll the value in
  // that case, or use 10 seconds.
  if (!empty() && !m_taskTimeout.is_queued()) {
    long timeout;
    curl_multi_timeout((CURLM*)m_handle, &timeout);
    priority_queue_insert(&taskScheduler,
                          &m_taskTimeout,
                          cachedTime +
                            torrent::utils::timer::from_milliseconds(
                              std::max<unsigned long>(timeout, 10000)));
  }
}

void
CurlStack::add_get(CurlGet* get) {
  if (!m_userAgent.empty())
    curl_easy_setopt(get->handle(), CURLOPT_USERAGENT, m_userAgent.c_str());

  if (!m_httpProxy.empty())
    curl_easy_setopt(get->handle(), CURLOPT_PROXY, m_httpProxy.c_str());

  if (!m_bindAddress.empty())
    curl_easy_setopt(get->handle(), CURLOPT_INTERFACE, m_bindAddress.c_str());

  if (!m_httpCaPath.empty())
    curl_easy_setopt(get->handle(), CURLOPT_CAPATH, m_httpCaPath.c_str());

  if (!m_httpCaCert.empty())
    curl_easy_setopt(get->handle(), CURLOPT_CAINFO, m_httpCaCert.c_str());

  curl_easy_setopt(
    get->handle(), CURLOPT_SSL_VERIFYHOST, (long)(m_ssl_verify_host ? 2 : 0));
  curl_easy_setopt(
    get->handle(), CURLOPT_SSL_VERIFYPEER, (long)(m_ssl_verify_peer ? 1 : 0));
  curl_easy_setopt(get->handle(), CURLOPT_DNS_CACHE_TIMEOUT, m_dns_timeout);

  base_type::push_back(get);

  if (m_active >= m_maxActive)
    return;

  m_active++;
  get->set_active(true);

  if (curl_multi_add_handle((CURLM*)m_handle, get->handle()) > 0)
    throw torrent::internal_error("Error calling curl_multi_add_handle.");
}

void
CurlStack::remove_get(CurlGet* get) {
  iterator itr = std::find(begin(), end(), get);

  if (itr == end())
    throw torrent::internal_error(
      "Could not find CurlGet when calling CurlStack::remove.");

  base_type::erase(itr);

  // The CurlGet object was never activated, so we just skip this one.
  if (!get->is_active())
    return;

  get->set_active(false);

  if (curl_multi_remove_handle((CURLM*)m_handle, get->handle()) > 0)
    throw torrent::internal_error("Error calling curl_multi_remove_handle.");

  if (m_active == m_maxActive &&
      (itr = std::find_if(
         begin(), end(), std::not_fn(std::mem_fn(&CurlGet::is_active)))) !=
        end()) {
    (*itr)->set_active(true);

    if (curl_multi_add_handle((CURLM*)m_handle, (*itr)->handle()) > 0)
      throw torrent::internal_error("Error calling curl_multi_add_handle.");

  } else {
    m_active--;
  }
}

void
CurlStack::global_init() {
  curl_global_init(CURL_GLOBAL_ALL);
}

void
CurlStack::global_cleanup() {
  curl_global_cleanup();
}

// TODO: Is this function supposed to set a per-handle timeout, or is
// it the shortest timeout amongst all handles?
int
CurlStack::set_timeout(void*, long timeout_ms, void* userp) {
  CurlStack* stack = (CurlStack*)userp;

  priority_queue_erase(&taskScheduler, &stack->m_taskTimeout);
  priority_queue_insert(&taskScheduler,
                        &stack->m_taskTimeout,
                        cachedTime +
                          torrent::utils::timer::from_milliseconds(timeout_ms));

  return 0;
}

}
