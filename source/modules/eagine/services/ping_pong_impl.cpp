/// @file
///
/// Copyright Matus Chochlik.
/// Distributed under the Boost Software License, Version 1.0.
/// See accompanying file LICENSE_1_0.txt or copy at
///  http://www.boost.org/LICENSE_1_0.txt
///
module eagine.msgbus.services;

import eagine.core.types;
import eagine.core.memory;
import eagine.core.identifier;
import eagine.core.utility;
import eagine.msgbus.core;
import <vector>;

namespace eagine::msgbus {
//------------------------------------------------------------------------------
class pinger_impl : public pinger_intf {
public:
    pinger_impl(subscriber& sub, pinger_signals& sigs) noexcept
      : base{sub}
      , signals{sigs} {}

    void add_methods() noexcept final {
        base.add_method(this, msgbus_map<"pong", &pinger_impl::_handle_pong>{});
    }

    void query_pingables() noexcept final {
        base.bus_node().query_subscribers_of(msgbus_id{"ping"});
    }

    void ping(
      const identifier_t pingable_id,
      const std::chrono::milliseconds max_time) noexcept final {
        message_view message{};
        auto msg_id{msgbus_id{"ping"}};
        message.set_target_id(pingable_id);
        message.set_priority(message_priority::low);
        base.bus_node().set_next_sequence_id(msg_id, message);
        base.bus_node().post(msg_id, message);
        _pending.emplace_back(message.target_id, message.sequence_no, max_time);
    }

    auto update() noexcept -> work_done final {
        some_true something_done{};

        something_done(
          std::erase_if(_pending, [this](auto& entry) {
              auto& [pingable_id, sequence_no, ping_time] = entry;
              if(ping_time.is_expired()) {
                  signals.ping_timeouted(
                    pingable_id,
                    sequence_no,
                    std::chrono::duration_cast<std::chrono::microseconds>(
                      ping_time.elapsed_time()));
                  return true;
              }
              return false;
          }) > 0);
        return something_done;
    }

    auto has_pending_pings() noexcept -> bool final {
        return !_pending.empty();
    }

private:
    auto _handle_pong(
      const message_context&,
      const stored_message& message) noexcept -> bool {
        std::erase_if(_pending, [this, &message](auto& entry) {
            auto& [pingable_id, sequence_no, ping_time] = entry;
            const bool is_response = (message.source_id == pingable_id) &&
                                     (message.sequence_no == sequence_no);
            if(is_response) {
                signals.ping_responded(
                  message.source_id,
                  message.sequence_no,
                  std::chrono::duration_cast<std::chrono::microseconds>(
                    ping_time.elapsed_time()),
                  base.verify_bits(message));
                return true;
            }
            return false;
        });
        return true;
    }

    subscriber& base;
    pinger_signals& signals;

    std::vector<std::tuple<identifier_t, message_sequence_t, timeout>>
      _pending{};
};
//------------------------------------------------------------------------------
auto make_pinger_impl(subscriber& base, pinger_signals& sigs)
  -> std::unique_ptr<pinger_intf> {
    return std::make_unique<pinger_impl>(base, sigs);
}
//------------------------------------------------------------------------------
} // namespace eagine::msgbus