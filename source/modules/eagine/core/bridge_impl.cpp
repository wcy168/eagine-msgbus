/// @file
///
/// Copyright Matus Chochlik.
/// Distributed under the Boost Software License, Version 1.0.
/// See accompanying file LICENSE_1_0.txt or copy at
/// https://www.boost.org/LICENSE_1_0.txt
///
module;

#include <cassert>

module eagine.msgbus.core;

import std;
import eagine.core.build_config;
import eagine.core.types;
import eagine.core.memory;
import eagine.core.string;
import eagine.core.identifier;
import eagine.core.serialization;
import eagine.core.utility;
import eagine.core.valid_if;
import eagine.core.runtime;
import eagine.core.main_ctx;

namespace eagine::msgbus {
//------------------------------------------------------------------------------
// bridge_state
//------------------------------------------------------------------------------
class bridge_state : public std::enable_shared_from_this<bridge_state> {
public:
    bridge_state(const valid_if_positive<span_size_t>& max_data_size) noexcept
      : _max_read{max_data_size.value_or(2048) * 2} {}
    bridge_state(bridge_state&&) = delete;
    bridge_state(const bridge_state&) = delete;
    auto operator=(bridge_state&&) = delete;
    auto operator=(const bridge_state&) = delete;
    ~bridge_state() noexcept {
        _output_ready.notify_all();
    }

    auto weak_ref() noexcept {
        return std::weak_ptr(this->shared_from_this());
    }

    auto make_input_main() noexcept {
        return [selfref{weak_ref()}]() {
            while(auto self{selfref.lock()}) {
                self->recv_input();
            }
        };
    }

    auto make_output_main() noexcept {
        return [selfref{weak_ref()}]() {
            while(auto self{selfref.lock()}) {
                self->send_output();
            }
        };
    }

    void start() noexcept {
        std::thread(make_input_main()).detach();
        std::thread(make_output_main()).detach();
    }

    auto input_usable() noexcept {
        const std::unique_lock lock{_input_mutex};
        return _input.good();
    }

    auto output_usable() noexcept {
        const std::unique_lock lock{_output_mutex};
        return _output.good();
    }

    auto is_usable() noexcept {
        return input_usable() and output_usable();
    }

    void push(const message_id msg_id, const message_view& message) noexcept {
        const std::unique_lock lock{_output_mutex};
        _outgoing.next().push(msg_id, message);
    }

    void notify_output_ready() noexcept {
        _output_ready.notify_one();
    }

    auto forwarded_messages() const noexcept {
        return _forwarded_messages;
    }

    auto dropped_messages() const noexcept {
        return _dropped_messages;
    }

    auto decode_errors() const noexcept {
        return _decode_errors;
    }

    void send_output() noexcept;

    using fetch_handler = message_storage::fetch_handler;

    auto fetch_messages(const fetch_handler handler) noexcept {
        auto& queue{[this]() -> message_storage& {
            const std::unique_lock lock{_input_mutex};
            _incoming.swap();
            return _incoming.current();
        }()};
        return queue.fetch_all(handler);
    }

    void recv_input() noexcept;

private:
    auto _make_send_handler() noexcept;

    void _do_recv_input(const span_size_t pos) noexcept;

    const span_size_t _max_read;

    std::mutex _input_mutex{};
    std::mutex _output_mutex{};

    std::condition_variable _output_ready{};

    std::istream& _input{std::cin};
    std::ostream& _output{std::cout};

    istream_data_source _source{_input};
    ostream_data_sink _sink{_output};

    memory::buffer _buffer{};
    double_buffer<message_storage> _outgoing{};
    double_buffer<message_storage> _incoming{};
    stored_message _recv_dest{};
    span_size_t _forwarded_messages{0};
    span_size_t _dropped_messages{0};
    span_size_t _decode_errors{0};
};
//------------------------------------------------------------------------------
auto bridge_state::_make_send_handler() noexcept {
    return
      [this](
        const message_id msg_id, const message_age msg_age, message_view message) {
          if(not message.add_age(msg_age).too_old()) [[likely]] {
              default_serializer_backend backend(_sink);
              if(serialize_message_header(msg_id, message, backend)) [[likely]] {
                  span_size_t i{0};
                  do_dissolve_bits(
                    make_span_getter(i, message.data()),
                    [this](byte b) {
                        const auto encode{make_base64_encode_transform()};
                        if(auto opt_c{encode(b)}) [[likely]] {
                            this->_output << *opt_c;
                            return true;
                        }
                        return false;
                    },
                    6);

                  _output << '\n' << std::flush;
                  ++_forwarded_messages;
              } else {
                  ++_dropped_messages;
              }
          } else {
              ++_dropped_messages;
          }
          return true;
      };
}
//------------------------------------------------------------------------------
void bridge_state::send_output() noexcept {
    const auto handler{_make_send_handler()};
    auto& queue{[this]() -> message_storage& {
        std::unique_lock lock{_output_mutex};
        _output_ready.wait(lock);
        _outgoing.swap();
        return _outgoing.current();
    }()};
    queue.fetch_all({construct_from, handler});
}
//------------------------------------------------------------------------------
void bridge_state::_do_recv_input(const span_size_t pos) noexcept {
    block_data_source source(_source.top(pos));
    default_deserializer_backend backend(source);
    identifier class_id{};
    identifier method_id{};
    _recv_dest.clear_data();

    if(deserialize_message_header(class_id, method_id, _recv_dest, backend))
      [[likely]] {
        _buffer.ensure(source.remaining().size());
        span_size_t i{0};
        span_size_t o{0};
        if(do_concentrate_bits(
             make_span_getter(
               i, source.remaining(), make_base64_decode_transform()),
             make_span_putter(o, cover(_buffer)),
             6)) {
            _recv_dest.store_content(head(view(_buffer), o));
        }

        const std::unique_lock lock{_input_mutex};
        _incoming.next().push({class_id, method_id}, _recv_dest);
    } else {
        ++_decode_errors;
    }
    _source.pop(pos + 1);
}
//------------------------------------------------------------------------------
void bridge_state::recv_input() noexcept {
    if(const auto pos{_source.scan_for('\n', _max_read)}) {
        _do_recv_input(*pos);
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
//------------------------------------------------------------------------------
// bridge
//------------------------------------------------------------------------------
bridge::bridge(main_ctx_parent parent) noexcept
  : main_ctx_object("MsgBusBrdg", parent)
  , _context{make_context(*this)} {
    _setup_from_config();
}
//------------------------------------------------------------------------------
auto bridge::_uptime_seconds() noexcept -> std::int64_t {
    return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::steady_clock::now() - _startup_time)
      .count();
}
//------------------------------------------------------------------------------
void bridge::add_certificate_pem(const memory::const_block blk) noexcept {
    if(_context) {
        _context->add_own_certificate_pem(blk);
    }
}
//------------------------------------------------------------------------------
void bridge::add_ca_certificate_pem(const memory::const_block blk) noexcept {
    if(_context) {
        _context->add_ca_certificate_pem(blk);
    }
}
//------------------------------------------------------------------------------
auto bridge::add_connection(shared_holder<connection> conn) noexcept -> bool {
    _connection = std::move(conn);
    return true;
}
//------------------------------------------------------------------------------
void bridge::_setup_from_config() {
    // TODO: use app_config()
}
//------------------------------------------------------------------------------
auto bridge::_handle_id_assigned(const message_view& message) noexcept
  -> message_handling_result {
    if(not has_id()) {
        _id = message.target_id;
        log_debug("assigned bridge id ${id} by router").arg("id", _id);
    }
    return was_handled;
}
//------------------------------------------------------------------------------
auto bridge::_handle_id_confirmed(const message_view& message) noexcept
  -> message_handling_result {
    if(has_id()) {
        if(_id != message.target_id) {
            log_error("mismatching current and confirmed ids")
              .arg("current", _id)
              .arg("confirmed", message.target_id);
        }
    } else {
        log_warning("confirming unset id ${newId}")
          .arg("confirmed", message.target_id);
    }
    return was_handled;
}
//------------------------------------------------------------------------------
auto bridge::_handle_ping(
  const message_view& message,
  const bool to_connection) noexcept -> message_handling_result {
    if(has_id()) [[likely]] {
        if(_id == message.target_id) {
            message_view response{};
            response.setup_response(message);
            response.set_source_id(_id);
            if(to_connection) {
                _do_push(msgbus_id{"pong"}, response);
            } else {
                _send(msgbus_id{"pong"}, response);
            }
            return was_handled;
        }
    }
    return should_be_forwarded;
}
//------------------------------------------------------------------------------
auto bridge::_handle_topo_bridge_conn(
  const message_view& message,
  const bool to_connection) noexcept -> message_handling_result {
    if(to_connection) {
        bridge_topology_info info{};
        if(default_deserialize(info, message.content())) [[likely]] {
            info.opposite_id = _id;
            auto temp{default_serialize_buffer_for(info)};
            if(auto serialized{default_serialize(info, cover(temp))}) {
                message_view response{message, *serialized};
                _send(msgbus_id{"topoBrdgCn"}, response);
                return was_handled;
            }
        }
    }
    return should_be_forwarded;
}
//------------------------------------------------------------------------------
auto bridge::_handle_topology_query(
  const message_view& message,
  const bool to_connection) noexcept -> message_handling_result {
    bridge_topology_info info{};
    info.bridge_id = _id;
    info.instance_id = _instance_id;
    auto temp{default_serialize_buffer_for(info)};
    if(const auto serialized{default_serialize(info, cover(temp))}) [[likely]] {
        message_view response{*serialized};
        response.setup_response(message);
        if(to_connection) {
            _do_push(msgbus_id{"topoBrdgCn"}, response);
        } else {
            _send(msgbus_id{"topoBrdgCn"}, response);
        }
    }
    return should_be_forwarded;
}
//------------------------------------------------------------------------------
auto bridge::_handle_stats_query(
  const message_view& message,
  const bool to_connection) noexcept -> message_handling_result {
    _stats.forwarded_messages = _forwarded_messages_i2c;
    _stats.dropped_messages = _dropped_messages_i2c;
    _stats.uptime_seconds = _uptime_seconds();

    const auto now{std::chrono::steady_clock::now()};
    const std::chrono::duration<float> seconds{now - _forwarded_since_stat};
    if(seconds.count() > 15.F) [[likely]] {
        _forwarded_since_stat = now;

        _stats.messages_per_second = static_cast<std::int32_t>(
          float(_stats.forwarded_messages - _prev_forwarded_messages) /
          seconds.count());
        _prev_forwarded_messages = _stats.forwarded_messages;
    }

    auto bs_buf{default_serialize_buffer_for(_stats)};
    if(const auto serialized{default_serialize(_stats, cover(bs_buf))}) {
        message_view response{*serialized};
        response.setup_response(message);
        response.set_source_id(_id);
        if(to_connection) {
            _do_push(msgbus_id{"statsBrdg"}, response);
        } else {
            _send(msgbus_id{"statsBrdg"}, response);
        }
    }
    return should_be_forwarded;
}
//------------------------------------------------------------------------------
auto bridge::_handle_special(
  const message_id msg_id,
  const message_view& message,
  const bool to_connection) noexcept -> message_handling_result {
    if(is_special_message(msg_id)) [[unlikely]] {
        log_debug("bridge handling special message ${message}")
          .tag("hndlSpcMsg")
          .arg("bridge", _id)
          .arg("message", msg_id)
          .arg("target", message.target_id)
          .arg("source", message.source_id);

        switch(msg_id.method_id()) {
            case id_v("assignId"):
                return _handle_id_assigned(message);
            case id_v("confirmId"):
                return _handle_id_confirmed(message);
            case id_v("ping"):
                return _handle_ping(message, to_connection);
            case id_v("topoBrdgCn"):
                return _handle_topo_bridge_conn(message, to_connection);
            case id_v("topoQuery"):
                return _handle_topology_query(message, to_connection);
            case id_v("statsQuery"):
                return _handle_stats_query(message, to_connection);
            case id_v("msgFlowInf"):
                return was_handled;
            default:
                break;
        }
    }
    return should_be_forwarded;
}
//------------------------------------------------------------------------------
auto bridge::_do_send(const message_id msg_id, message_view& message) noexcept
  -> bool {
    message.add_hop();
    if(_connection) [[likely]] {
        if(_connection->send(msg_id, message)) {
            log_trace("forwarding message ${message} to connection")
              .arg("message", msg_id)
              .arg("data", message.data());
            return true;
        }
    }
    return false;
}
//------------------------------------------------------------------------------
auto bridge::_send(const message_id msg_id, message_view& message) noexcept
  -> bool {
    assert(has_id());
    message.set_source_id(_id);
    return _do_send(msg_id, message);
}
//------------------------------------------------------------------------------
auto bridge::_do_push(const message_id msg_id, message_view& message) noexcept
  -> bool {
    if(_state) [[likely]] {
        message.add_hop();
        _state->push(msg_id, message);
        log_trace("forwarding message ${message} to stream")
          .arg("message", msg_id)
          .arg("data", message.data());
        return true;
    }
    return false;
}
//------------------------------------------------------------------------------
auto bridge::_avg_msg_age_c2o() const noexcept -> std::chrono::microseconds {
    return std::chrono::duration_cast<std::chrono::microseconds>(
      _message_age_sum_c2o / (_forwarded_messages_c2o + _dropped_messages_c2o + 1));
}
//------------------------------------------------------------------------------
auto bridge::_avg_msg_age_i2c() const noexcept -> std::chrono::microseconds {
    return std::chrono::duration_cast<std::chrono::microseconds>(
      _message_age_sum_i2c / (_forwarded_messages_i2c + _dropped_messages_i2c + 1));
}
//------------------------------------------------------------------------------
constexpr auto bridge_log_stat_msg_count() noexcept {
    return debug_build ? 100'000 : 1'000'000;
}
//------------------------------------------------------------------------------
auto bridge::_should_log_bridge_stats_c2o() noexcept -> bool {
    return ++_forwarded_messages_c2o % bridge_log_stat_msg_count() == 0;
}
//------------------------------------------------------------------------------
auto bridge::_should_log_bridge_stats_i2c() noexcept -> bool {
    return ++_forwarded_messages_i2c % bridge_log_stat_msg_count() == 0;
}
//------------------------------------------------------------------------------
void bridge::_log_bridge_stats_c2o() noexcept {
    const auto now{std::chrono::steady_clock::now()};
    const std::chrono::duration<float> interval{now - _forwarded_since_c2o};

    if(interval > decltype(interval)::zero()) [[likely]] {
        const auto msgs_per_sec{
          float(bridge_log_stat_msg_count()) / interval.count()};

        log_chart_sample("msgPerSecO", msgs_per_sec);
        log_stat("forwarded ${count} messages to output (${msgsPerSec})")
          .tag("msgStats")
          .arg("count", _forwarded_messages_c2o)
          .arg("dropped", _dropped_messages_c2o)
          .arg("interval", interval)
          .arg("avgMsgAge", _avg_msg_age_c2o())
          .arg("msgsPerSec", "RatePerSec", msgs_per_sec);
    }

    _forwarded_since_c2o = now;
}
//------------------------------------------------------------------------------
void bridge::_log_bridge_stats_i2c() noexcept {
    const auto now{std::chrono::steady_clock::now()};
    const std::chrono::duration<float> interval{now - _forwarded_since_i2c};

    if(interval > decltype(interval)::zero()) [[likely]] {
        const auto msgs_per_sec{
          float(bridge_log_stat_msg_count()) / interval.count()};

        _stats.message_age_milliseconds =
          std::chrono::duration_cast<std::chrono::milliseconds>(_avg_msg_age_i2c())
            .count();

        log_chart_sample("msgPerSecI", msgs_per_sec);
        log_stat("forwarded ${count} messages from input (${msgsPerSec})")
          .tag("msgStats")
          .arg("count", _forwarded_messages_i2c)
          .arg("dropped", _dropped_messages_i2c)
          .arg("interval", interval)
          .arg("avgMsgAge", _avg_msg_age_i2c())
          .arg("msgsPerSec", "RatePerSec", msgs_per_sec);
    }

    _forwarded_since_i2c = now;
}
//------------------------------------------------------------------------------
auto bridge::_forward_messages() noexcept -> work_done {
    some_true something_done{};

    const auto forward_conn_to_output{
      [this](const message_id msg_id, message_age msg_age, message_view message) {
          _message_age_sum_c2o += message.add_age(msg_age).age();
          if(message.too_old()) [[unlikely]] {
              ++_dropped_messages_c2o;
              return true;
          }
          if(_should_log_bridge_stats_c2o()) [[unlikely]] {
              _log_bridge_stats_c2o();
          }
          if(_handle_special(msg_id, message, false) == was_handled) {
              return true;
          }
          return this->_do_push(msg_id, message);
      }};

    if(_connection) [[likely]] {
        something_done(
          _connection->fetch_messages({construct_from, forward_conn_to_output}));
    }
    _state->notify_output_ready();

    if(_state) [[likely]] {
        const auto forward_input_to_conn{
          [this](
            const message_id msg_id, message_age msg_age, message_view message) {
              _message_age_sum_i2c += message.add_age(msg_age).age();
              if(message.too_old()) [[unlikely]] {
                  ++_dropped_messages_i2c;
                  return true;
              }
              if(_should_log_bridge_stats_i2c()) [[unlikely]] {
                  _log_bridge_stats_i2c();
              }
              if(this->_handle_special(msg_id, message, true) == was_handled) {
                  return true;
              }
              this->_do_send(msg_id, message);
              return true;
          }};

        something_done(
          _state->fetch_messages({construct_from, forward_input_to_conn}));
    }

    return something_done;
}
//------------------------------------------------------------------------------
auto bridge::_recoverable_state() const noexcept -> bool {
    return std::cin.good() and std::cout.good();
}
//------------------------------------------------------------------------------
auto bridge::_check_state() noexcept -> work_done {
    some_true something_done{};

    if(not(_state and _state->is_usable())) [[unlikely]] {
        if(_recoverable_state() and _connection) {
            if(const auto max_data_size{_connection->max_data_size()}) {
                ++_state_count;
                _state.emplace(*max_data_size);
                _state->start();
                something_done();
            }
        }
    }

    return something_done;
}
//------------------------------------------------------------------------------
auto bridge::_update_connections() noexcept -> work_done {
    some_true something_done{};

    if(_connection) [[likely]] {
        if(not has_id() and _no_id_timeout) [[unlikely]] {
            log_debug("requesting bridge id");
            _connection->send(msgbus_id{"requestId"}, {});
            _no_id_timeout.reset();
            something_done();
        }
        if(_connection->update()) {
            something_done();
            _no_connection_timeout.reset();
        }
    }
    return something_done;
}
//------------------------------------------------------------------------------
auto bridge::update() noexcept -> work_done {
    static const auto exec_time_id{register_time_interval("busUpdate")};
    const auto exec_time{measure_time_interval(exec_time_id)};
    some_true something_done{};

    const bool had_id = has_id();
    something_done(_check_state());
    something_done(_update_connections());
    something_done(_forward_messages());

    // if processing the messages assigned the id
    if(has_id() and not had_id) [[unlikely]] {
        log_debug("announcing id ${id}").arg("id", _id);
        message_view msg;
        _send(msgbus_id{"announceId"}, msg);
        something_done();
    }

    return something_done;
}
//------------------------------------------------------------------------------
auto bridge::is_done() const noexcept -> bool {
    return no_connection_timeout() or not _recoverable_state();
}
//------------------------------------------------------------------------------
void bridge::say_bye() noexcept {
    const auto msgid = msgbus_id{"byeByeBrdg"};
    message_view msg{};
    msg.set_source_id(_id);
    if(_connection) {
        _connection->send(msgid, msg);
        _connection->update();
    }
    if(_state) {
        _do_push(msgid, msg);
        _state->notify_output_ready();
        std::this_thread::sleep_for(std::chrono::seconds{1});
    }
    _forward_messages();
    _update_connections();
}
//------------------------------------------------------------------------------
void bridge::cleanup() noexcept {
    if(_connection) {
        _connection->cleanup();
    }
    const auto avg_msg_age_c2o =
      _message_age_sum_c2o /
      float(_forwarded_messages_c2o + _dropped_messages_c2o + 1);
    const auto avg_msg_age_i2c =
      _message_age_sum_i2c /
      float(_forwarded_messages_i2c + _dropped_messages_i2c + 1);

    if(_state) {
        log_stat("forwarded ${count} messages in total to output stream")
          .tag("msgStats")
          .arg("count", _state->forwarded_messages())
          .arg("dropped", _state->dropped_messages())
          .arg("decodeErr", _state->decode_errors())
          .arg("stateCount", _state_count);
    }

    log_stat("forwarded ${count} messages in total to output queue")
      .tag("msgStats")
      .arg("count", _forwarded_messages_c2o)
      .arg("dropped", _dropped_messages_c2o)
      .arg("avgMsgAge", avg_msg_age_c2o);

    log_stat("forwarded ${count} messages in total to connection")
      .tag("msgStats")
      .arg("count", _forwarded_messages_i2c)
      .arg("dropped", _dropped_messages_i2c)
      .arg("avgMsgAge", avg_msg_age_i2c);
}
//------------------------------------------------------------------------------
void bridge::finish() noexcept {
    say_bye();
    timeout too_long{adjusted_duration(std::chrono::seconds{1})};
    while(not too_long) {
        update();
    }
    cleanup();
}
//------------------------------------------------------------------------------
} // namespace eagine::msgbus
