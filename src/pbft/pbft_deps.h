// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include "libbyz/Cycle_counter.h"
#include "libbyz/ITimer.h"
#include "libbyz/Statistics.h"
#include "libbyz/Time.h"
#include "libbyz/network.h"
#include "libbyz/types.h"
#include "node/nodetonode.h"
#include "raft/rafttypes.h"

#include <signal.h>

std::vector<ITimer*> ITimer::timers;
Time ITimer::min_deadline = Long_max;
Time ITimer::_relative_current_time = 0;

ITimer::ITimer(int t, void (*h)())
{
  state = stopped;

  period = t * clock_mhz;

  handler = h;
  timers.push_back(this);
}

ITimer::~ITimer()
{
  for (size_t i = 0; i < timers.size(); i++)
  {
    if (timers[i] == this)
    {
      timers[i] = timers.back();
      timers.pop_back();
      break;
    }
  }
}

void ITimer::start()
{
  if (state != stopped)
  {
    return;
  }
  restart();
}

void ITimer::restart()
{
  if (state != stopped && state != expired)
  {
    return;
  }

  state = running;

  deadline = _relative_current_time;
  deadline += period;

  if (deadline < min_deadline)
  {
    min_deadline = deadline;
  }
}

void ITimer::adjust(int t)
{
  period = t;
}

void ITimer::stop()
{
  if (state != running)
  {
    return;
  }

  state = stopped;
}

void ITimer::restop()
{
  state = stopped;
}

void ITimer::handle_timeouts(std::chrono::milliseconds elapsed)
{
  _relative_current_time += elapsed.count();
  _handle_timeouts(_relative_current_time);
}

void ITimer::_handle_timeouts(Time current)
{
  min_deadline = 9223372036854775807LL;

  for (size_t i = 0; i < timers.size(); i++)
  {
    ITimer* timer = timers[i];

    if (timer->state == running)
    {
      if (timer->deadline < current)
      {
        timer->state = expired;
        timer->handler();
      }
      else
      {
        if (timer->deadline < min_deadline)
        {
          min_deadline = timer->deadline;
        }
      }
    }
  }
}

Time ITimer::current_time()
{
  return _relative_current_time;
}

Time ITimer::length_100_ms()
{
  return 100;
}

Time ITimer::length_10_ms()
{
  return ITimer::length_100_ms() / 10;
}

long long clock_mhz = 1;

void init_clock_mhz() {}

Time zero_time()
{
  return 0;
}

long long diff_time(Time t1, Time t2)
{
  return (t1 - t2) / clock_mhz;
}

bool less_than_time(Time t1, Time t2)
{
  return t1 < t2;
}

Statistics stats;

Statistics::Statistics() : rec_stats(20) {}

void Statistics::zero_stats() {}

void Statistics::print_stats() {}

Recovery_stats::Recovery_stats() {}

void Statistics::init_rec_stats() {}

void Statistics::end_rec_stats() {}

void Recovery_stats::zero_stats() {}

void Recovery_stats::print_stats() {}

class PbftEnclaveNetwork : public INetwork
{
public:
  PbftEnclaveNetwork(
    raft::NodeId id, std::shared_ptr<ccf::NodeToNode> n2n_channels) :
    n2n_channels(n2n_channels),
    id(id)
  {}

  virtual ~PbftEnclaveNetwork() = default;

  bool Initialize(in_port_t port) override
  {
    return true;
  }

  int Send(void* buf, uint32_t size, IPrincipal& principal) override
  {
    raft::NodeId to = principal.pid();
    raft::RaftHeader hdr = {raft::RaftMsgType::pbft_message, id};

    // TODO: Encrypt msg here
    std::vector<uint8_t> msg(sizeof(raft::RaftHeader) + size);
    auto data_ = msg.data();
    auto space = msg.size();
    serialized::write<raft::RaftHeader>(data_, space, hdr);
    serialized::write(
      data_, space, reinterpret_cast<const uint8_t*>(buf), size);

    n2n_channels->send_authenticated(to, msg);
    return size;
  }

  virtual void GetNextMessage(
    void* buf, size_t msize, size_t size, uint32_t message_rep_size) override
  {
    assert("Should not be called");
    return;
  }

  virtual bool has_messages(long to) override
  {
    return false;
  }

private:
  std::shared_ptr<ccf::NodeToNode> n2n_channels;
  raft::NodeId id;
};