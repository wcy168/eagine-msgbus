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
import eagine.core.types;
import eagine.core.memory;
import eagine.core.build_info;
import eagine.core.identifier;
import eagine.core.container;
import eagine.core.reflection;
import eagine.core.units;
import eagine.core.utility;
import eagine.core.valid_if;
import eagine.core.runtime;
import eagine.core.main_ctx;

namespace eagine::msgbus {
//------------------------------------------------------------------------------
class remote_host_impl {
public:
    timeout is_alive{adjusted_duration(std::chrono::seconds{300})};
    timeout should_query_sensors{std::chrono::seconds{10}};
    std::string hostname;
    span_size_t cpu_concurrent_threads{-1};
    span_size_t total_ram_size{-1};
    span_size_t total_swap_size{-1};
    variable_with_history<span_size_t, 2> free_ram_size{-1};
    variable_with_history<span_size_t, 2> free_swap_size{-1};
    variable_with_history<float, 2> short_average_load{-1.F};
    variable_with_history<float, 2> long_average_load{-1.F};

    variable_with_history<float, 2> min_temperature{0.F};
    variable_with_history<float, 2> max_temperature{0.F};

    remote_host_changes changes{};
    power_supply_kind power_supply{power_supply_kind::unknown};
    bool was_alive{false};
};
//------------------------------------------------------------------------------
class remote_instance_impl {
public:
    timeout is_alive{adjusted_duration(std::chrono::seconds{180})};
    string_view app_name;
    optionally_valid<compiler_info> cmplr_info;
    optionally_valid<version_info> ver_info;
    host_id_t host_id{0U};

    remote_instance_changes changes{};
    bool was_alive{false};
};
//------------------------------------------------------------------------------
class remote_node_impl {
public:
    process_instance_id_t instance_id{0U};
    string_view display_name;
    string_view description;
    tribool is_router_node{indeterminate};
    tribool is_bridge_node{indeterminate};
    host_id_t host_id{0U};

    timeout should_ping{std::chrono::seconds{5}};
    span_size_t pings_sent{0};
    span_size_t pings_responded{0};
    span_size_t pings_timeouted{0};
    std::chrono::microseconds last_ping_time{};
    std::chrono::microseconds last_ping_timeout{};
    std::chrono::microseconds message_age{};
    std::chrono::seconds uptime{};

    std::int64_t sent_messages{-1};
    std::int64_t received_messages{-1};
    std::int64_t dropped_messages{-1};
    std::int32_t messages_per_second{-1};
    std::uint8_t ping_bits{0};
    node_kind kind{node_kind::unknown};

    remote_node_changes changes{};

    auto get_sub(const message_id msg_id) const noexcept -> tribool {
        return find(_subscriptions, msg_id).and_then(_1.to_tribool());
    }

    auto get_sub(const message_id msg_id) noexcept -> tribool& {
        auto sub{find(_subscriptions, msg_id)};
        if(not sub) {
            sub.emplace(msg_id, indeterminate);
        }
        return *sub;
    }

    void clear() noexcept {
        *this = remote_node_impl();
    }

private:
    flat_map<message_id, tribool> _subscriptions;
};
//------------------------------------------------------------------------------
class node_connection_impl {
public:
    float block_usage_ratio{-1.F};
    float bytes_per_second{-1.F};
    connection_kind kind{connection_kind::unknown};
};
//------------------------------------------------------------------------------
// remote_instance
//------------------------------------------------------------------------------
inline auto remote_instance::_impl() const noexcept
  -> optional_reference<const remote_instance_impl> {
    return _pimpl.ref();
}
//------------------------------------------------------------------------------
inline auto remote_instance::_impl() noexcept
  -> optional_reference<remote_instance_impl> {
    return _pimpl.ensure();
}
//------------------------------------------------------------------------------
auto remote_instance::is_alive() const noexcept -> bool {
    if(auto impl{_impl()}) {
        return not impl->is_alive.is_expired();
    }
    return false;
}
//------------------------------------------------------------------------------
auto remote_instance::host() const noexcept -> remote_host {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        if(i.host_id) {
            return _tracker.get_host(i.host_id);
        }
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_instance::application_name() const noexcept
  -> valid_if_not_empty<string_view> {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        return {i.app_name};
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_instance::compiler() const noexcept
  -> optional_reference<const compiler_info> {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        if(i.cmplr_info) {
            return {*i.cmplr_info};
        }
    }
    return {nothing};
}
//------------------------------------------------------------------------------
auto remote_instance::build_version() const noexcept
  -> optional_reference<const version_info> {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        if(i.ver_info) {
            return {*i.ver_info};
        }
    }
    return {nothing};
}
//------------------------------------------------------------------------------
// remote_instance_state
//------------------------------------------------------------------------------
auto remote_instance_state::update() noexcept -> remote_instance_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        const auto alive = i.is_alive.is_expired();
        if(i.was_alive != alive) {
            i.was_alive = alive;
            i.changes |= alive ? remote_instance_change::started_responding
                               : remote_instance_change::stopped_responding;
        }
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_instance_state::changes() noexcept -> remote_instance_changes {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        const auto result = i.changes;
        i.changes.clear();
        return result;
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_instance_state::add_change(const remote_instance_change change) noexcept
  -> remote_instance_state& {
    if(auto impl{_impl()}) {
        impl->changes |= change;
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_instance_state::notice_alive() noexcept -> remote_instance_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        if(not i.is_alive) {
            i.changes |= remote_instance_change::started_responding;
        }
        i.is_alive.reset();
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_instance_state::set_host_id(const host_id_t host_id) noexcept
  -> remote_instance_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        if(i.host_id != host_id) {
            i.host_id = host_id;
            i.changes |= remote_instance_change::host_id;
        }
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_instance_state::set_app_name(const std::string& new_app_name) noexcept
  -> remote_instance_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        auto app_name = _tracker.cached(new_app_name);
        if(not are_equal(app_name, i.app_name)) {
            i.app_name = app_name;
            i.changes |= remote_instance_change::application_info;
        }
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_instance_state::assign(compiler_info info) noexcept
  -> remote_instance_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        if(not i.cmplr_info) {
            i.cmplr_info = {std::move(info), true};
            i.changes |= remote_instance_change::build_info;
        }
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_instance_state::assign(version_info info) noexcept
  -> remote_instance_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        if(not i.ver_info) {
            i.ver_info = {std::move(info), true};
            i.changes |= remote_instance_change::build_info;
        }
    }
    return *this;
}
//------------------------------------------------------------------------------
// remote_host
//------------------------------------------------------------------------------
inline auto remote_host::_impl() const noexcept
  -> optional_reference<const remote_host_impl> {
    return _pimpl.ref();
}
//------------------------------------------------------------------------------
inline auto remote_host::_impl() noexcept -> optional_reference<remote_host_impl> {
    return _pimpl.ensure();
}
//------------------------------------------------------------------------------
auto remote_host_state::update() noexcept -> remote_host_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        const auto alive = i.is_alive.is_expired();
        if(i.was_alive != alive) {
            i.was_alive = alive;
            i.changes |= alive ? remote_host_change::started_responding
                               : remote_host_change::stopped_responding;
        }
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_host_state::changes() noexcept -> remote_host_changes {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        const auto result = i.changes;
        i.changes.clear();
        return result;
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_host_state::add_change(const remote_host_change change) noexcept
  -> remote_host_state& {
    if(auto impl{_impl()}) {
        impl->changes |= change;
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_host::is_alive() const noexcept -> bool {
    if(auto impl{_impl()}) {
        return not impl->is_alive.is_expired();
    }
    return false;
}
//------------------------------------------------------------------------------
auto remote_host::name() const noexcept -> valid_if_not_empty<string_view> {
    if(auto impl{_impl()}) {
        return {impl->hostname};
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_host::cpu_concurrent_threads() const noexcept
  -> valid_if_positive<span_size_t> {
    if(auto impl{_impl()}) {
        return {impl->cpu_concurrent_threads};
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_host::short_average_load() const noexcept
  -> valid_if_nonnegative<float> {
    if(auto impl{_impl()}) {
        return {impl->short_average_load.value()};
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_host::short_average_load_change() const noexcept
  -> optionally_valid<float> {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        return {
          i.short_average_load.delta(), i.short_average_load.old_value() >= 0.F};
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_host::long_average_load() const noexcept
  -> valid_if_nonnegative<float> {
    if(auto impl{_impl()}) {
        return {impl->long_average_load.value()};
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_host::long_average_load_change() const noexcept
  -> optionally_valid<float> {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        return {
          i.long_average_load.delta(), i.long_average_load.old_value() >= 0.F};
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_host::total_ram_size() const noexcept
  -> valid_if_positive<span_size_t> {
    if(auto impl{_impl()}) {
        return {impl->total_ram_size};
    }
    return {-1};
}
//------------------------------------------------------------------------------
auto remote_host::free_ram_size() const noexcept -> valid_if_positive<span_size_t> {
    if(auto impl{_impl()}) {
        return {impl->free_ram_size.value()};
    }
    return {-1};
}
//------------------------------------------------------------------------------
auto remote_host::free_ram_size_change() const noexcept
  -> optionally_valid<span_size_t> {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        return {i.free_ram_size.delta(), i.free_ram_size.old_value() > 0};
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_host::total_swap_size() const noexcept
  -> valid_if_positive<span_size_t> {
    if(auto impl{_impl()}) {
        return {impl->total_swap_size};
    }
    return {-1};
}
//------------------------------------------------------------------------------
auto remote_host::free_swap_size() const noexcept
  -> valid_if_nonnegative<span_size_t> {
    if(auto impl{_impl()}) {
        return {impl->free_swap_size.value()};
    }
    return {-1};
}
//------------------------------------------------------------------------------
auto remote_host::free_swap_size_change() const noexcept
  -> optionally_valid<span_size_t> {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        return {i.free_swap_size.delta(), i.free_swap_size.old_value() >= 0};
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_host::min_temperature() const noexcept
  -> valid_if_positive<kelvins_t<float>> {
    if(auto impl{_impl()}) {
        return {kelvins_(impl->min_temperature.value())};
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_host::max_temperature() const noexcept
  -> valid_if_positive<kelvins_t<float>> {
    if(auto impl{_impl()}) {
        return {kelvins_(impl->max_temperature.value())};
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_host::min_temperature_change() const noexcept
  -> optionally_valid<kelvins_t<float>> {
    if(auto impl{_impl()}) {
        return {
          kelvins_(impl->min_temperature.delta()),
          impl->min_temperature.old_value() > 0.F};
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_host::max_temperature_change() const noexcept
  -> optionally_valid<kelvins_t<float>> {
    if(auto impl{_impl()}) {
        return {
          kelvins_(impl->max_temperature.delta()),
          impl->max_temperature.old_value() > 0.F};
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_host::power_supply() const noexcept -> power_supply_kind {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        return i.power_supply;
    }
    return {};
}
//------------------------------------------------------------------------------
// remote_node
//------------------------------------------------------------------------------
inline auto remote_node::_impl() const noexcept
  -> optional_reference<const remote_node_impl> {
    return _pimpl.ref();
}
//------------------------------------------------------------------------------
inline auto remote_node::_impl() noexcept -> optional_reference<remote_node_impl> {
    return _pimpl.ensure();
}
//------------------------------------------------------------------------------
auto remote_node::instance_id() const noexcept
  -> valid_if_not_zero<process_instance_id_t> {
    if(auto impl{_impl()}) {
        return {impl->instance_id};
    }
    return {0U};
}
//------------------------------------------------------------------------------
auto remote_node::kind() const noexcept -> node_kind {
    if(auto impl{_impl()}) {
        return impl->kind;
    }
    return node_kind::unknown;
}
//------------------------------------------------------------------------------
auto remote_node::has_endpoint_info() const noexcept -> bool {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        return not i.is_router_node.is(indeterminate) and
               not i.is_bridge_node.is(indeterminate) and
               not i.display_name.empty();
    }
    return false;
}
//------------------------------------------------------------------------------
auto remote_node::display_name() const noexcept -> valid_if_not_empty<string_view> {
    if(auto impl{_impl()}) {
        return impl->display_name;
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_node::description() const noexcept -> valid_if_not_empty<string_view> {
    if(auto impl{_impl()}) {
        return impl->description;
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_node::is_router_node() const noexcept -> tribool {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        const auto k = i.kind;
        if((k == node_kind::router) or (k == node_kind::bridge)) {
            return false;
        }
        return i.is_router_node;
    }
    return indeterminate;
}
//------------------------------------------------------------------------------
auto remote_node::is_bridge_node() const noexcept -> tribool {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        const auto k = i.kind;
        if((k == node_kind::router) or (k == node_kind::bridge)) {
            return false;
        }
        return i.is_bridge_node;
    }
    return indeterminate;
}
//------------------------------------------------------------------------------
auto remote_node::host_id() const noexcept -> valid_if_not_zero<host_id_t> {
    if(auto impl{_impl()}) {
        return impl->host_id;
    }
    return {0U};
}
//------------------------------------------------------------------------------
auto remote_node::host() const noexcept -> remote_host {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        if(i.host_id) {
            return _tracker.get_host(i.host_id);
        }
        if(auto inst{instance()}) {
            return inst.host();
        }
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_node::instance() const noexcept -> remote_instance {
    if(auto impl{_impl()}) {
        return _tracker.get_instance(impl->instance_id);
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_node::sent_messages() const noexcept
  -> valid_if_nonnegative<std::int64_t> {
    if(auto impl{_impl()}) {
        const auto& i = *impl;
        return {i.sent_messages};
    }
    return {-1};
}
//------------------------------------------------------------------------------
auto remote_node::received_messages() const noexcept
  -> valid_if_nonnegative<std::int64_t> {
    if(auto impl{_impl()}) {
        const auto& i = *impl;
        return {i.received_messages};
    }
    return {-1};
}
//------------------------------------------------------------------------------
auto remote_node::dropped_messages() const noexcept
  -> valid_if_nonnegative<std::int64_t> {
    if(auto impl{_impl()}) {
        const auto& i = *impl;
        return {i.dropped_messages};
    }
    return {-1};
}
//------------------------------------------------------------------------------
auto remote_node::messages_per_second() const noexcept
  -> valid_if_nonnegative<int> {
    if(auto impl{_impl()}) {
        const auto& i = *impl;
        return {eagine::limit_cast<int>(i.messages_per_second)};
    }
    return {-1};
}
//------------------------------------------------------------------------------
auto remote_node::average_message_age() const noexcept
  -> valid_if_not_zero<std::chrono::microseconds> {
    if(auto impl{_impl()}) {
        const auto& i = *impl;
        return {i.message_age};
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_node::uptime() const noexcept
  -> valid_if_not_zero<std::chrono::seconds> {
    if(auto impl{_impl()}) {
        const auto& i = *impl;
        return {i.uptime};
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_node::connections() const noexcept -> node_connections {
    std::vector<endpoint_id_t> remote_ids;
    _tracker.for_each_connection([&](const auto& conn) {
        if(auto remote_id{conn.opposite_id(_node_id)}) {
            remote_ids.push_back(*remote_id);
        }
    });
    return {_node_id, std::move(remote_ids), _tracker};
}
//------------------------------------------------------------------------------
auto remote_node::subscribes_to(const message_id msg_id) const noexcept -> tribool {
    if(auto impl{_impl()}) {
        return impl->get_sub(msg_id);
    }
    return indeterminate;
}
//------------------------------------------------------------------------------
auto remote_node::can_query_system_info() const noexcept -> tribool {
    return subscribes_to(message_id{"eagiSysInf", "qryStats"}) or
           subscribes_to(message_id{"eagiSysInf", "qrySensors"});
}
//------------------------------------------------------------------------------
auto remote_node::is_pingable() const noexcept -> tribool {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        if(i.kind == node_kind::router or i.kind == node_kind::bridge) {
            return true;
        }
        return impl->get_sub(msgbus_id{"ping"});
    }
    return indeterminate;
}
//------------------------------------------------------------------------------
void remote_node::set_ping_interval(const std::chrono::milliseconds ms) noexcept {
    if(auto impl{_impl()}) {
        impl->should_ping.reset(ms, nothing);
    }
}
//------------------------------------------------------------------------------
auto remote_node::ping_roundtrip_time() const noexcept
  -> valid_if_not_zero<std::chrono::microseconds> {
    if(auto impl{_impl()}) {
        if(impl->pings_responded > 0) {
            return {impl->last_ping_time};
        }
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_node::ping_success_rate() const noexcept
  -> valid_if_between_0_1<float> {
    if(auto impl{_impl()}) {
        if(impl->pings_sent > 0) {
            return {float(impl->pings_responded) / float(impl->pings_sent)};
        }
    }
    return {-1.F};
}
//------------------------------------------------------------------------------
auto remote_node::is_responsive() const noexcept -> tribool {
    if(auto impl{_impl()}) {
        return bool(impl->ping_bits);
    }
    return indeterminate;
}
//------------------------------------------------------------------------------
// remote_node_state
//------------------------------------------------------------------------------
auto remote_node_state::clear() noexcept -> remote_node_state& {
    if(auto impl{_impl()}) {
        impl->clear();
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_node_state::host_state() const noexcept -> remote_host_state {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        if(i.host_id) {
            return _tracker.get_host(i.host_id);
        }
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_node_state::instance_state() const noexcept -> remote_instance_state {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        if(i.instance_id) {
            return _tracker.get_instance(i.instance_id);
        }
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_node_state::update() noexcept -> remote_node_state& {
    return *this;
}
//------------------------------------------------------------------------------
auto remote_node_state::changes() noexcept -> remote_node_changes {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        const auto result = i.changes;
        i.changes.clear();
        return result;
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_node_state::add_change(const remote_node_change change) noexcept
  -> remote_node_state& {
    if(auto impl{_impl()}) {
        impl->changes |= change;
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_node_state::set_instance_id(
  const process_instance_id_t instance_id) noexcept -> remote_node_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        if(i.instance_id != instance_id) {
            i.instance_id = instance_id;
            i.changes |= remote_node_change::instance_id;
        }
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_node_state::set_host_id(const host_id_t host_id) noexcept
  -> remote_node_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        if(i.host_id != host_id) {
            i.host_id = host_id;
            i.changes |= remote_node_change::host_id;
            if(i.instance_id) {
                _tracker.get_instance(i.instance_id).set_host_id(host_id);
            }
        }
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_node_state::assign(const node_kind kind) noexcept
  -> remote_node_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        if(i.kind != kind) {
            i.kind = kind;
            i.changes |= remote_node_change::kind;
        }
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_node_state::assign(const endpoint_info& info) noexcept
  -> remote_node_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        if(i.kind != node_kind::endpoint) {
            i.kind = node_kind::endpoint;
            i.changes |= remote_node_change::kind;
        }
        auto display_name = _tracker.cached(info.display_name);
        if(not are_equal(display_name, i.display_name)) {
            i.display_name = display_name;
            i.changes |= remote_node_change::endpoint_info;
        }
        auto description = _tracker.cached(info.description);
        if(not are_equal(description, i.description)) {
            i.description = description;
            i.changes |= remote_node_change::endpoint_info;
        }
        if(i.is_router_node.is(indeterminate)) {
            i.is_router_node = info.is_router_node;
            i.changes |= remote_node_change::endpoint_info;
        }
        if(i.is_bridge_node.is(indeterminate)) {
            i.is_bridge_node = info.is_bridge_node;
            i.changes |= remote_node_change::endpoint_info;
        }
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_node_state::assign(const router_statistics& stats) noexcept
  -> remote_node_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        if(i.kind != node_kind::router) {
            i.kind = node_kind::router;
            i.changes |= remote_node_change::kind;
        }
        i.sent_messages = stats.forwarded_messages;
        i.dropped_messages = stats.dropped_messages;
        i.messages_per_second = stats.messages_per_second;
        i.message_age = std::chrono::microseconds{stats.message_age_us};
        i.uptime = std::chrono::seconds{stats.uptime_seconds};
        i.changes |= remote_node_change::statistics;
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_node_state::assign(const bridge_statistics& stats) noexcept
  -> remote_node_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        if(i.kind != node_kind::bridge) {
            i.kind = node_kind::bridge;
            i.changes |= remote_node_change::kind;
        }
        i.sent_messages = stats.forwarded_messages;
        i.dropped_messages = stats.dropped_messages;
        i.messages_per_second = stats.messages_per_second;
        i.message_age = std::chrono::milliseconds{stats.message_age_milliseconds};
        i.uptime = std::chrono::seconds{stats.uptime_seconds};
        i.changes |= remote_node_change::statistics;
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_node_state::assign(const endpoint_statistics& stats) noexcept
  -> remote_node_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        if(i.kind != node_kind::endpoint) {
            i.kind = node_kind::endpoint;
            i.changes |= remote_node_change::kind;
        }
        i.sent_messages = stats.sent_messages;
        i.received_messages = stats.received_messages;
        i.dropped_messages = stats.dropped_messages;
        i.uptime = std::chrono::seconds{stats.uptime_seconds};
        i.changes |= remote_node_change::statistics;
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_node_state::add_subscription(const message_id msg_id) noexcept
  -> remote_node_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        auto& s = i.get_sub(msg_id);
        if(not s.is(true)) {
            s = true;
            i.changes |= remote_node_change::methods_added;
        }
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_node_state::remove_subscription(const message_id msg_id) noexcept
  -> remote_node_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        auto& s = i.get_sub(msg_id);
        if(not s.is(false)) {
            s = false;
            i.changes |= remote_node_change::methods_removed;
        }
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_node_state::should_ping() noexcept
  -> std::tuple<bool, std::chrono::milliseconds> {
    if(auto impl{_impl()}) {
        auto& to = impl->should_ping;
        return {
          to.is_expired(),
          std::chrono::duration_cast<std::chrono::milliseconds>(to.period() * 2)};
    }
    return {false, {}};
}
//------------------------------------------------------------------------------
auto remote_node_state::notice_alive() noexcept -> remote_node_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        const auto was_responsive = bool(i.ping_bits);
        i.ping_bits <<= 1U;
        i.ping_bits |= 1U;
        if(was_responsive != bool(i.ping_bits)) {
            i.changes |= remote_node_change::started_responding;
            if(i.instance_id) {
                _tracker.get_instance(i.instance_id)
                  .add_change(remote_instance_change::started_responding);
            }
        }
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_node_state::pinged() noexcept -> remote_node_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        i.should_ping.reset();
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_node_state::ping_response(
  const message_sequence_t,
  const std::chrono::microseconds age) noexcept -> remote_node_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        const auto was_responsive = bool(i.ping_bits);
        i.last_ping_time = age;
        i.ping_bits <<= 1U;
        i.ping_bits |= 1U;
        ++i.pings_sent;
        ++i.pings_responded;
        if(was_responsive != bool(i.ping_bits)) {
            i.changes |= remote_node_change::started_responding;
        }
        i.changes |= remote_node_change::response_rate;
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_node_state::ping_timeout(
  const message_sequence_t,
  const std::chrono::microseconds age) noexcept -> remote_node_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        const auto was_responsive = bool(i.ping_bits);
        i.last_ping_timeout = age;
        i.ping_bits <<= 1U;
        ++i.pings_sent;
        ++i.pings_timeouted;
        if(was_responsive != bool(i.ping_bits)) {
            i.changes |= remote_node_change::stopped_responding;
        }
        i.changes |= remote_node_change::response_rate;
    }
    return *this;
}
//------------------------------------------------------------------------------
// remote_host_state
//------------------------------------------------------------------------------
auto remote_host_state::should_query_sensors() const noexcept -> bool {
    if(const auto impl{_impl()}) {
        return impl->should_query_sensors.is_expired();
    }
    return false;
}
//------------------------------------------------------------------------------
auto remote_host_state::sensors_queried() noexcept -> remote_host_state& {
    if(auto impl{_impl()}) {
        impl->should_query_sensors.reset();
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_host_state::notice_alive() noexcept -> remote_host_state& {
    if(auto impl{_impl()}) {
        impl->is_alive.reset();
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_host_state::set_hostname(std::string hn) noexcept
  -> remote_host_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        i.hostname = std::move(hn);
        i.changes |= remote_host_change::hostname;
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_host_state::set_cpu_concurrent_threads(const span_size_t value) noexcept
  -> remote_host_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        i.cpu_concurrent_threads = value;
        i.changes |= remote_host_change::hardware_config;
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_host_state::set_short_average_load(const float value) noexcept
  -> remote_host_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        i.short_average_load = value;
        i.changes |= remote_host_change::sensor_values;
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_host_state::set_long_average_load(const float value) noexcept
  -> remote_host_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        i.long_average_load = value;
        i.changes |= remote_host_change::sensor_values;
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_host_state::set_total_ram_size(const span_size_t value) noexcept
  -> remote_host_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        i.total_ram_size = value;
        i.changes |= remote_host_change::hardware_config;
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_host_state::set_total_swap_size(const span_size_t value) noexcept
  -> remote_host_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        i.total_swap_size = value;
        i.changes |= remote_host_change::hardware_config;
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_host_state::set_free_ram_size(const span_size_t value) noexcept
  -> remote_host_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        i.free_ram_size = value;
        i.changes |= remote_host_change::sensor_values;
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_host_state::set_free_swap_size(const span_size_t value) noexcept
  -> remote_host_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        i.free_swap_size = value;
        i.changes |= remote_host_change::sensor_values;
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_host_state::set_temperature_min_max(
  const kelvins_t<float> min,
  const kelvins_t<float> max) noexcept -> remote_host_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        i.min_temperature = min.value();
        i.max_temperature = max.value();
        i.changes |= remote_host_change::sensor_values;
    }
    return *this;
}
//------------------------------------------------------------------------------
auto remote_host_state::set_power_supply(const power_supply_kind value) noexcept
  -> remote_host_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        i.power_supply = value;
        i.changes |= remote_host_change::sensor_values;
    }
    return *this;
}
//------------------------------------------------------------------------------
// node_connection
//------------------------------------------------------------------------------
inline auto node_connection::_impl() const noexcept
  -> optional_reference<const node_connection_impl> {
    return _pimpl.ref();
}
//------------------------------------------------------------------------------
inline auto node_connection::_impl() noexcept
  -> optional_reference<node_connection_impl> {
    return _pimpl.ensure();
}
//------------------------------------------------------------------------------
auto node_connection::kind() const noexcept -> connection_kind {
    if(auto impl{_impl()}) {
        return impl->kind;
    }
    return connection_kind::unknown;
}
//------------------------------------------------------------------------------
auto node_connection::block_usage_ratio() const noexcept
  -> valid_if_nonnegative<float> {
    if(auto impl{_impl()}) {
        return {impl->block_usage_ratio};
    }
    return {-1.F};
}
//------------------------------------------------------------------------------
auto node_connection::bytes_per_second() const noexcept
  -> valid_if_nonnegative<float> {
    if(auto impl{_impl()}) {
        return {impl->bytes_per_second};
    }
    return {-1.F};
}
//------------------------------------------------------------------------------
// node_connection_state
//------------------------------------------------------------------------------
auto node_connection_state::set_kind(const connection_kind kind) noexcept
  -> node_connection_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        if(i.kind != kind) {
            i.kind = kind;
            _tracker.get_node(_id1).add_change(remote_node_change::connection_info);
            _tracker.get_node(_id2).add_change(remote_node_change::connection_info);
        }
    }
    return *this;
}
//------------------------------------------------------------------------------
auto node_connection_state::assign(const connection_statistics& stats) noexcept
  -> node_connection_state& {
    if(auto impl{_impl()}) {
        auto& i = *impl;
        i.block_usage_ratio = stats.block_usage_ratio;
        i.bytes_per_second = stats.bytes_per_second;
        _tracker.get_node(stats.local_id)
          .notice_alive()
          .add_change(remote_node_change::connection_info);
        _tracker.get_node(stats.remote_id)
          .add_change(remote_node_change::connection_info);
    }
    return *this;
}
//------------------------------------------------------------------------------
// remote_node_tracker
//------------------------------------------------------------------------------
class remote_node_tracker_impl {
public:
    flat_map<endpoint_id_t, remote_node_state> nodes;
    flat_map<process_instance_id_t, remote_instance_state> instances;
    flat_map<host_id_t, remote_host_state> hosts;
    std::vector<node_connection_state> connections;

    auto cached(const std::string& s) noexcept -> string_view {
        auto cs{eagine::find(_string_cache, s)};
        if(not cs) {
            cs.insert(s);
        }
        return {*cs};
    }

private:
    std::set<std::string> _string_cache;
};
//------------------------------------------------------------------------------
remote_node_tracker::remote_node_tracker() noexcept
  : _pimpl{hold<remote_node_tracker_impl>} {}
//------------------------------------------------------------------------------
auto remote_node_tracker::cached(const std::string& s) noexcept -> string_view {
    assert(_pimpl);
    return _pimpl->cached(s);
}
//------------------------------------------------------------------------------
auto remote_node_tracker::_get_nodes() noexcept
  -> flat_map<endpoint_id_t, remote_node_state>& {
    assert(_pimpl);
    return _pimpl->nodes;
}
//------------------------------------------------------------------------------
auto remote_node_tracker::_get_instances() noexcept
  -> flat_map<process_instance_id_t, remote_instance_state>& {
    assert(_pimpl);
    return _pimpl->instances;
}
//------------------------------------------------------------------------------
auto remote_node_tracker::_get_hosts() noexcept
  -> flat_map<host_id_t, remote_host_state>& {
    assert(_pimpl);
    return _pimpl->hosts;
}
//------------------------------------------------------------------------------
auto remote_node_tracker::_get_connections() noexcept
  -> std::vector<node_connection_state>& {
    assert(_pimpl);
    return _pimpl->connections;
}
//------------------------------------------------------------------------------
auto remote_node_tracker::_get_connections() const noexcept
  -> const std::vector<node_connection_state>& {
    assert(_pimpl);
    return _pimpl->connections;
}
//------------------------------------------------------------------------------
auto remote_node_tracker::get_node(const endpoint_id_t node_id) noexcept
  -> remote_node_state& {
    assert(_pimpl);
    assert(node_id != 0U);
    auto node{find(_pimpl->nodes, node_id)};
    if(not node) {
        node.emplace(node_id, node_id, _pimpl);
    }
    assert(node->id() == node_id);
    return *node;
}
//------------------------------------------------------------------------------
auto remote_node_tracker::remove_node(const endpoint_id_t node_id) noexcept
  -> bool {
    assert(_pimpl);
    return _pimpl->nodes.erase(node_id) > 0;
}
//------------------------------------------------------------------------------
auto remote_node_tracker::get_host(const host_id_t host_id) noexcept
  -> remote_host_state& {
    assert(_pimpl);
    auto host{find(_pimpl->hosts, host_id)};
    if(not host) {
        host.emplace(host_id, host_id);
    }
    assert(host->id() == host_id);
    return *host;
}
//------------------------------------------------------------------------------
auto remote_node_tracker::get_host(const host_id_t host_id) const noexcept
  -> remote_host_state {
    if(_pimpl) {
        if(const auto host{find(_pimpl->hosts, host_id)}) {
            return *host;
        }
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_node_tracker::get_instance(
  const process_instance_id_t instance_id) noexcept -> remote_instance_state& {
    assert(_pimpl);
    auto inst{find(_pimpl->instances, instance_id)};
    if(not inst) {
        inst.emplace(instance_id, instance_id, _pimpl);
    }
    assert(inst->id() == instance_id);
    return *inst;
}
//------------------------------------------------------------------------------
auto remote_node_tracker::get_instance(
  const process_instance_id_t instance_id) const noexcept -> remote_instance_state {
    if(_pimpl) {
        if(const auto inst{find(_pimpl->instances, instance_id)}) {
            return *inst;
        }
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_node_tracker::get_connection(
  const endpoint_id_t node_id1,
  const endpoint_id_t node_id2) noexcept -> node_connection_state& {
    assert(_pimpl);
    for(auto& conn : _pimpl->connections) {
        if(conn.connects(node_id1, node_id2)) {
            return conn;
        }
    }
    _pimpl->connections.emplace_back(node_id1, node_id2, _pimpl);
    get_node(node_id1).add_change(remote_node_change::connection_info);
    get_node(node_id2).add_change(remote_node_change::connection_info);
    return _pimpl->connections.back();
}
//------------------------------------------------------------------------------
auto remote_node_tracker::get_connection(
  const endpoint_id_t node_id1,
  const endpoint_id_t node_id2) const noexcept -> node_connection_state {
    if(_pimpl) {
        for(auto& conn : _pimpl->connections) {
            if(conn.connects(node_id1, node_id2)) {
                return conn;
            }
        }
    }
    return {};
}
//------------------------------------------------------------------------------
auto remote_node_tracker::notice_instance(
  const endpoint_id_t node_id,
  const process_instance_id_t instance_id) noexcept -> remote_node_state& {
    auto& node = get_node(node_id);
    if(const auto node_inst{node.instance_id()}) {
        // if node instance changed
        if(*node_inst != instance_id) {
            // clear the node state
            node.clear();
            // remove connection info
            auto& connections = _get_connections();

            std::erase_if(connections, [=](const auto& conn) {
                return conn.connects(node_id);
            });

            node.set_instance_id(instance_id);
            if(auto host_id{node.host_id()}) {
                get_instance(instance_id).notice_alive().set_host_id(*host_id);
            }
        } else {
            get_instance(instance_id).notice_alive();
        }
    } else {
        node.set_instance_id(instance_id);
        if(auto host_id{node.host_id()}) {
            get_instance(instance_id).notice_alive().set_host_id(*host_id);
        }
    }
    return node.notice_alive();
}
} // namespace eagine::msgbus
//------------------------------------------------------------------------------
