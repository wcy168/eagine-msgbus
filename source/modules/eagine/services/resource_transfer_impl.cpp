/// @file
///
/// Copyright Matus Chochlik.
/// Distributed under the Boost Software License, Version 1.0.
/// See accompanying file LICENSE_1_0.txt or copy at
/// https://www.boost.org/LICENSE_1_0.txt
///
module;

#include <cassert>

module eagine.msgbus.services;

import std;
import eagine.core.types;
import eagine.core.memory;
import eagine.core.string;
import eagine.core.identifier;
import eagine.core.container;
import eagine.core.valid_if;
import eagine.core.utility;
import eagine.core.runtime;
import eagine.core.logging;
import eagine.core.math;
import eagine.core.main_ctx;
import eagine.msgbus.core;

namespace eagine::msgbus {
//------------------------------------------------------------------------------
// single_byte_blob_io
//------------------------------------------------------------------------------
class single_byte_blob_io final : public source_blob_io {
public:
    single_byte_blob_io(const span_size_t size, const byte value) noexcept
      : _size{size}
      , _value{value} {}

    auto total_size() noexcept -> span_size_t final {
        return _size;
    }

    auto fetch_fragment(const span_size_t offs, memory::block dst) noexcept
      -> span_size_t final {
        return fill(head(dst, _size - offs), _value).size();
    }

private:
    span_size_t _size;
    byte _value;
};
//------------------------------------------------------------------------------
// sequence_blob_io
//------------------------------------------------------------------------------
class sequence_blob_io final : public source_blob_io {
    struct _generator {
        using seq_t = std::uint64_t;
        static constexpr auto sequence_bytes() noexcept -> span_size_t {
            return span_size_of<seq_t>();
        }

        static auto reverse_bytes(seq_t s) noexcept -> seq_t;

        auto operator()() noexcept -> byte;

        _generator(const span_size_t offs) noexcept
          : _mod{limit_cast<seq_t>(offs % sequence_bytes())}
          , _seq{limit_cast<seq_t>(offs / sequence_bytes())}
          , _cur{reverse_bytes(_seq)} {}

        seq_t _mod;
        seq_t _seq;
        seq_t _cur;
        span_size_t _ofs{0};
    };

public:
    sequence_blob_io(const span_size_t size) noexcept
      : _size{size} {}

    auto total_size() noexcept -> span_size_t final {
        return _size;
    }

    auto fetch_fragment(const span_size_t offs, memory::block dst) noexcept
      -> span_size_t final {
        return generate(head(dst, _size - offs), _generator(offs)).size();
    }

private:
    span_size_t _size;
};
//------------------------------------------------------------------------------
auto sequence_blob_io::_generator::reverse_bytes(seq_t s) noexcept -> seq_t {
    seq_t r{0U};
    for(span_size_t i = 1; i < sequence_bytes(); ++i) {
        r = r | (0xFFU & s);
        r = r << 8U;
        s = s >> 8U;
    }
    return r | (0xFFU & s);
}
//------------------------------------------------------------------------------
auto sequence_blob_io::_generator::operator()() noexcept -> byte {
    while(_mod > 0U) {
        --_mod;
        ++_ofs;
        _cur = _cur >> 8U;
    }
    const auto result{byte(0xFFU & _cur)};
    if(++_ofs < sequence_bytes()) {
        _cur = _cur >> 8U;
    } else {
        _cur = reverse_bytes(++_seq);
        _ofs = 0;
    }
    return result;
}
//------------------------------------------------------------------------------
// random_byte_blob_io
//------------------------------------------------------------------------------
class random_byte_blob_io final : public source_blob_io {
public:
    random_byte_blob_io(span_size_t size) noexcept
      : _size{size}
      , _re{std::random_device{}()} {}

    auto total_size() noexcept -> span_size_t final {
        return _size;
    }

    auto fetch_fragment(const span_size_t offs, memory::block dst) noexcept
      -> span_size_t final {
        return fill_with_random_bytes(head(dst, _size - offs), _re).size();
    }

private:
    span_size_t _size;
    std::default_random_engine _re;
};
//------------------------------------------------------------------------------
// file_blob_io
//------------------------------------------------------------------------------
class file_blob_io final
  : public source_blob_io
  , public target_blob_io {
public:
    file_blob_io(
      std::fstream file,
      std::optional<span_size_t> offs,
      std::optional<span_size_t> size) noexcept;

    auto is_at_eod(const span_size_t offs) noexcept -> bool final {
        return offs >= total_size();
    }

    auto total_size() noexcept -> span_size_t final {
        return _size - _offs;
    }

    auto fetch_fragment(const span_size_t offs, memory::block dst) noexcept
      -> span_size_t final {
        _file.seekg(_offs + offs, std::ios::beg);
        return limit_cast<span_size_t>(
          read_from_stream(_file, head(dst, _size - _offs - offs)).gcount());
    }

    auto store_fragment(
      const span_size_t offs,
      const memory::const_block src,
      const blob_info&) noexcept -> bool final {
        _file.seekg(_offs + offs, std::ios::beg);
        return write_to_stream(_file, head(src, _size - _offs - offs)).good();
    }

    auto check_stored(const span_size_t, memory::const_block) noexcept
      -> bool final {
        return true;
    }

    void handle_finished(
      const message_id,
      const message_age,
      const message_info&,
      const blob_info&) noexcept final {
        _file.close();
    }

    void handle_cancelled() noexcept final {
        _file.close();
    }

private:
    std::fstream _file;
    span_size_t _offs{0};
    span_size_t _size{0};
};
//------------------------------------------------------------------------------
file_blob_io::file_blob_io(
  std::fstream file,
  std::optional<span_size_t> offs,
  std::optional<span_size_t> size) noexcept
  : _file{std::move(file)} {
    _file.seekg(0, std::ios::end);
    _size = static_cast<span_size_t>(_file.tellg());
    if(size) {
        _size = _size ? math::minimum(_size, *size) : *size;
    }
    if(offs) {
        _offs = math::minimum(_size, *offs);
    }
}
// resource_server_impl
//------------------------------------------------------------------------------
class resource_server_impl : public resource_server_intf {
public:
    resource_server_impl(subscriber& sub, resource_server_driver& drvr) noexcept;

    void add_methods() noexcept final;

    auto update() noexcept -> work_done final;

    auto has_pending_blobs() noexcept -> bool final {
        return _blobs.has_outgoing() or base.bus_node().has_outgoing_blobs();
    }

    void average_message_age(const std::chrono::microseconds age) noexcept final {
        _should_send_outgoing.set_duration(std::min(
          std::chrono::microseconds{50} + age / 16,
          std::chrono::microseconds{50000}));
    }

    void set_file_root(const std::filesystem::path& root_path) noexcept final {
        _root_path = std::filesystem::canonical(root_path);
    }

    void notify_resource_available(const string_view locator) noexcept final;

    auto is_contained(const std::filesystem::path& file_path) const noexcept
      -> bool;

    auto get_file_path(const url& locator) const noexcept -> std::filesystem::path;

    auto has_resource(const message_context&, const url& locator) noexcept -> bool;

    auto get_resource(
      const message_context& ctx,
      const url& locator,
      const endpoint_id_t endpoint_id,
      const message_priority priority) -> std::
      tuple<shared_holder<source_blob_io>, std::chrono::seconds, message_priority>;

private:
    auto _handle_has_resource_query(
      const message_context& ctx,
      const stored_message& message) noexcept -> bool;

    auto _handle_resource_content_request(
      const message_context& ctx,
      const stored_message& message) noexcept -> bool;

    auto _handle_resource_resend_request(
      const message_context& ctx,
      const stored_message& message) noexcept -> bool;

    subscriber& base;
    resource_server_driver& driver;
    blob_manipulator _blobs;
    timeout _should_send_outgoing{std::chrono::microseconds{1}};
    std::filesystem::path _root_path{};
};
//------------------------------------------------------------------------------
auto make_resource_server_impl(subscriber& sub, resource_server_driver& drvr)
  -> unique_holder<resource_server_intf> {
    return {hold<resource_server_impl>, sub, drvr};
}
//------------------------------------------------------------------------------
resource_server_impl::resource_server_impl(
  subscriber& sub,
  resource_server_driver& drvr) noexcept
  : base{sub}
  , driver{drvr}
  , _blobs{
      base,
      message_id{"eagiRsrces", "fragment"},
      message_id{"eagiRsrces", "fragResend"},
      message_id{"eagiRsrces", "blobPrpare"}} {}
//------------------------------------------------------------------------------
void resource_server_impl::add_methods() noexcept {
    base.add_method(
      this,
      message_map<
        "eagiRsrces",
        "qryResurce",
        &resource_server_impl::_handle_has_resource_query>{});
    base.add_method(
      this,
      message_map<
        "eagiRsrces",
        "getContent",
        &resource_server_impl::_handle_resource_content_request>{});
    base.add_method(
      this,
      message_map<
        "eagiRsrces",
        "fragResend",
        &resource_server_impl::_handle_resource_resend_request>{});
}
//------------------------------------------------------------------------------
auto resource_server_impl::update() noexcept -> work_done {
    auto& bus = base.bus_node();
    some_true something_done{
      _blobs.update(bus.post_callable(), min_connection_data_size)};
    if(_should_send_outgoing) {
        something_done(_blobs.process_outgoing(
          bus.post_callable(), min_connection_data_size, 2));
        _should_send_outgoing.reset();
    }

    return something_done;
}
//------------------------------------------------------------------------------
void resource_server_impl::notify_resource_available(
  const string_view locator) noexcept {
    auto buffer = default_serialize_buffer_for(locator);

    if(const auto serialized{default_serialize(locator, cover(buffer))})
      [[likely]] {
        const auto msg_id{message_id{"eagiRsrces", "available"}};
        message_view message{*serialized};
        message.set_target_id(broadcast_endpoint_id());
        base.bus_node().post(msg_id, message);
    }
}
//------------------------------------------------------------------------------
auto resource_server_impl::is_contained(
  const std::filesystem::path& file_path) const noexcept -> bool {
    return starts_with(string_view(file_path), string_view(_root_path));
}
//------------------------------------------------------------------------------
auto resource_server_impl::get_file_path(const url& locator) const noexcept
  -> std::filesystem::path {
    try {
        if(const auto loc_path_str{locator.path_str()}) {
            std::filesystem::path loc_path{std::string_view{*loc_path_str}};
            if(_root_path.empty()) {
                if(loc_path.is_absolute()) {
                    return loc_path;
                }
                return std::filesystem::current_path().root_path() / loc_path;
            }
            if(loc_path.is_absolute()) {
                return std::filesystem::canonical(
                  _root_path / loc_path.relative_path());
            }
            return std::filesystem::canonical(_root_path / loc_path);
        }
    } catch(const std::exception&) {
    }
    return {};
}
//------------------------------------------------------------------------------
auto resource_server_impl::has_resource(
  const message_context&,
  const url& locator) noexcept -> bool {
    if(const auto has_res{driver.has_resource(locator)}) {
        return true;
    } else if(has_res.is(indeterminate)) {
        if(locator.has_scheme("eagires")) {
            return locator.has_path("/zeroes") or locator.has_path("/ones") or
                   locator.has_path("/sequence") or locator.has_path("/random");
        } else if(locator.has_scheme("file")) {
            const auto file_path = get_file_path(locator);
            if(is_contained(file_path)) {
                try {
                    const auto stat = std::filesystem::status(file_path);
                    return exists(stat) and not is_directory(stat);
                } catch(...) {
                }
            }
        }
    }
    return false;
}
//------------------------------------------------------------------------------
auto resource_server_impl::get_resource(
  const message_context& ctx,
  const url& locator,
  const endpoint_id_t endpoint_id,
  const message_priority priority) -> std::
  tuple<shared_holder<source_blob_io>, std::chrono::seconds, message_priority> {
    auto read_io{driver.get_resource_io(endpoint_id, locator)};
    if(not read_io) {
        if(locator.has_scheme("eagires")) {
            if(const auto count{locator.argument("count")}) {
                if(const auto bytes{from_string<span_size_t>(*count)}) {
                    if(locator.has_path("/random")) {
                        read_io.emplace_derived(hold<random_byte_blob_io>, *bytes);
                    } else if(locator.has_path("/zeroes")) {
                        read_io.emplace_derived(
                          hold<single_byte_blob_io>, *bytes, 0x0U);
                    } else if(locator.has_path("/ones")) {
                        read_io.emplace_derived(
                          hold<single_byte_blob_io>, *bytes, 0x1U);
                    } else if(locator.has_path("/sequence")) {
                        read_io.emplace_derived(hold<sequence_blob_io>, *bytes);
                    }
                }
            }
        } else if(locator.has_scheme("file")) {
            const auto file_path = get_file_path(locator);
            if(is_contained(file_path)) {
                std::fstream file{file_path, std::ios::in | std::ios::binary};
                if(file.is_open()) {
                    ctx.bus_node()
                      .log_info("sending file ${filePath} to ${target}")
                      .arg("target", endpoint_id)
                      .arg("filePath", "FsPath", file_path);
                    read_io.emplace_derived(
                      hold<file_blob_io>,
                      std::move(file),
                      from_string<span_size_t>(
                        locator.argument("offs").or_default())
                        .to_optional(),
                      from_string<span_size_t>(
                        locator.argument("size").or_default())
                        .to_optional());
                }
            }
        }
    }

    const auto max_time =
      read_io ? driver.get_blob_timeout(endpoint_id, locator, read_io->total_size())
              : std::chrono::seconds{};

    return {
      std::move(read_io),
      max_time,
      driver.get_blob_priority(endpoint_id, locator, priority)};
}
//------------------------------------------------------------------------------
auto resource_server_impl::_handle_has_resource_query(
  const message_context& ctx,
  const stored_message& message) noexcept -> bool {
    std::string url_str;
    if(default_deserialize(url_str, message.content())) [[likely]] {
        const url locator{std::move(url_str)};
        if(has_resource(ctx, locator)) {
            message_view response{message.content()};
            response.setup_response(message);
            ctx.bus_node().post(message_id{"eagiRsrces", "hasResurce"}, response);
        } else {
            message_view response{message.content()};
            response.setup_response(message);
            ctx.bus_node().post(message_id{"eagiRsrces", "hasNotRsrc"}, response);
        }
    }
    return true;
}
//------------------------------------------------------------------------------
auto resource_server_impl::_handle_resource_content_request(
  const message_context& ctx,
  const stored_message& message) noexcept -> bool {
    std::string url_str;
    if(default_deserialize(url_str, message.content())) [[likely]] {
        const url locator{std::move(url_str)};
        ctx.bus_node()
          .log_info("received content request for ${url}")
          .tag("rsrcCntReq")
          .arg("url", "URL", locator.str());

        auto [read_io, max_time, priority] =
          get_resource(ctx, locator, message.source_id, message.priority);
        if(read_io) {
            _blobs.push_outgoing(
              message_id{"eagiRsrces", "content"},
              message.target_id,
              message.source_id,
              message.sequence_no,
              std::move(read_io),
              max_time,
              priority);
        } else {
            message_view response{};
            response.setup_response(message);
            ctx.bus_node().post(message_id{"eagiRsrces", "notFound"}, response);
            ctx.bus_node()
              .log_info("failed to get I/O object for content request")
              .arg("url", "URL", locator.str());
        }
    } else {
        ctx.bus_node()
          .log_error("failed to deserialize resource content request")
          .arg("content", message.const_content());
    }
    return true;
}
//------------------------------------------------------------------------------
auto resource_server_impl::_handle_resource_resend_request(
  const message_context&,
  const stored_message& message) noexcept -> bool {
    _blobs.process_resend(message);
    return true;
}
//------------------------------------------------------------------------------
// resource_manipulator_impl
//------------------------------------------------------------------------------
class resource_manipulator_impl : public resource_manipulator_intf {
public:
    resource_manipulator_impl(
      subscriber& sub,
      resource_manipulator_signals& sigs) noexcept
      : base{sub}
      , signals{sigs} {}

    void init(
      subscriber_discovery_signals& discovery,
      host_info_consumer_signals& host_info) noexcept final;

    void add_methods() noexcept final;

    auto update() noexcept -> work_done final;

    auto server_endpoint_id(const url& locator) noexcept -> endpoint_id_t final;

    auto search_resource(
      const endpoint_id_t endpoint_id,
      const url& locator) noexcept -> std::optional<message_sequence_t> final;

    auto query_resource_content(
      endpoint_id_t endpoint_id,
      const url& locator,
      shared_holder<target_blob_io> write_io,
      const message_priority priority,
      const std::chrono::seconds max_time)
      -> std::optional<message_sequence_t> final;

private:
    void _handle_alive(
      const result_context&,
      const subscriber_alive& alive) noexcept;

    void _handle_subscribed(
      const result_context&,
      const subscriber_subscribed& sub) noexcept;

    void _remove_server(const endpoint_id_t endpoint_id) noexcept;

    void _handle_unsubscribed(
      const result_context&,
      const subscriber_unsubscribed& sub) noexcept;

    void _handle_not_subscribed(
      const result_context&,
      const subscriber_not_subscribed& sub) noexcept;

    void _handle_host_id_received(
      const result_context& ctx,
      const valid_if_positive<host_id_t>& host_id) noexcept;

    void _handle_hostname_received(
      const result_context& ctx,
      const valid_if_not_empty<std::string>& hostname) noexcept;

    auto _handle_has_resource(
      const message_context&,
      const stored_message& message) noexcept -> bool;

    auto _handle_has_not_resource(
      const message_context&,
      const stored_message& message) noexcept -> bool;

    auto _handle_resource_fragment(
      [[maybe_unused]] const message_context& ctx,
      const stored_message& message) noexcept -> bool;

    auto _handle_resource_not_found(
      const message_context&,
      const stored_message& message) noexcept -> bool;

    auto _handle_resource_resend_request(
      const message_context&,
      const stored_message& message) noexcept -> bool;

    auto _handle_resource_prepare(
      const message_context&,
      const stored_message& message) noexcept -> bool;

    auto _handle_resource_available(
      const message_context&,
      const stored_message& message) noexcept -> bool;

    subscriber& base;
    resource_manipulator_signals& signals;
    blob_manipulator _blobs{
      base.bus_node(),
      message_id{"eagiRsrces", "fragment"},
      message_id{"eagiRsrces", "fragResend"},
      message_id{"eagiRsrces", "blobPrpare"}};

    resetting_timeout _search_servers{std::chrono::seconds{5}, nothing};

    flat_map<std::string, flat_set<endpoint_id_t>, str_view_less>
      _hostname_to_endpoint;
    flat_map<identifier_t, flat_set<endpoint_id_t>> _host_id_to_endpoint;

    struct _server_info {
        std::chrono::steady_clock::time_point last_report_time{};
    };

    flat_map<endpoint_id_t, _server_info> _server_endpoints;
};
//------------------------------------------------------------------------------
void resource_manipulator_impl::_handle_alive(
  const result_context&,
  const subscriber_alive& alive) noexcept {
    const auto pos{_server_endpoints.find(alive.source.endpoint_id)};
    if(pos != _server_endpoints.end()) {
        auto& svr_info = std::get<1>(*pos);
        svr_info.last_report_time = std::chrono::steady_clock::now();
    }
}
//------------------------------------------------------------------------------
void resource_manipulator_impl::_handle_subscribed(
  const result_context&,
  const subscriber_subscribed& sub) noexcept {
    if(sub.message_type.is("eagiRsrces", "getContent")) {
        auto spos{_server_endpoints.find(sub.source.endpoint_id)};
        if(spos == _server_endpoints.end()) {
            spos = _server_endpoints.emplace(sub.source.endpoint_id).first;
            signals.resource_server_appeared(sub.source.endpoint_id);
        }
        auto& svr_info = std::get<1>(*spos);
        svr_info.last_report_time = std::chrono::steady_clock::now();
    }
}
//------------------------------------------------------------------------------
void resource_manipulator_impl::_remove_server(
  const endpoint_id_t endpoint_id) noexcept {
    const auto spos = _server_endpoints.find(endpoint_id);
    if(spos != _server_endpoints.end()) {
        signals.resource_server_lost(endpoint_id);
        _server_endpoints.erase(spos);
    }
    for(auto& entry : _host_id_to_endpoint) {
        std::get<1>(entry).erase(endpoint_id);
    }
    _host_id_to_endpoint.erase_if(
      [](const auto& entry) { return std::get<1>(entry).empty(); });

    for(auto& entry : _hostname_to_endpoint) {
        std::get<1>(entry).erase(endpoint_id);
    }
    _hostname_to_endpoint.erase_if(
      [](const auto& entry) { return std::get<1>(entry).empty(); });
}
//------------------------------------------------------------------------------
void resource_manipulator_impl::_handle_unsubscribed(
  const result_context&,
  const subscriber_unsubscribed& sub) noexcept {
    if(sub.message_type.is("eagiRsrces", "getContent")) {
        _remove_server(sub.source.endpoint_id);
    }
}
//------------------------------------------------------------------------------
void resource_manipulator_impl::_handle_not_subscribed(
  const result_context&,
  const subscriber_not_subscribed& sub) noexcept {
    if(sub.message_type.is("eagiRsrces", "getContent")) {
        _remove_server(sub.source.endpoint_id);
    }
}
//------------------------------------------------------------------------------
void resource_manipulator_impl::_handle_host_id_received(
  const result_context& ctx,
  const valid_if_positive<host_id_t>& host_id) noexcept {
    if(host_id) {
        _host_id_to_endpoint[*host_id].insert(ctx.source_id());
    }
}
//------------------------------------------------------------------------------
void resource_manipulator_impl::_handle_hostname_received(
  const result_context& ctx,
  const valid_if_not_empty<std::string>& hostname) noexcept {
    if(hostname) {
        _hostname_to_endpoint[*hostname].insert(ctx.source_id());
    }
}
//------------------------------------------------------------------------------
auto resource_manipulator_impl::_handle_has_resource(
  const message_context&,
  const stored_message& message) noexcept -> bool {
    std::string url_str;
    if(default_deserialize(url_str, message.content())) [[likely]] {
        const url locator{std::move(url_str)};
        signals.server_has_resource(message.source_id, locator);
    }
    return true;
}
//------------------------------------------------------------------------------
auto resource_manipulator_impl::_handle_has_not_resource(
  const message_context&,
  const stored_message& message) noexcept -> bool {
    std::string url_str;
    if(default_deserialize(url_str, message.content())) [[likely]] {
        const url locator{std::move(url_str)};
        signals.server_has_not_resource(message.source_id, locator);
    }
    return true;
}
//------------------------------------------------------------------------------
auto resource_manipulator_impl::_handle_resource_fragment(
  [[maybe_unused]] const message_context& ctx,
  const stored_message& message) noexcept -> bool {
    _blobs.process_incoming(message);
    return true;
}
//------------------------------------------------------------------------------
auto resource_manipulator_impl::_handle_resource_not_found(
  const message_context&,
  const stored_message& message) noexcept -> bool {
    _blobs.cancel_incoming(message.sequence_no);
    return true;
}
//------------------------------------------------------------------------------
auto resource_manipulator_impl::_handle_resource_resend_request(
  const message_context&,
  const stored_message& message) noexcept -> bool {
    _blobs.process_resend(message);
    return true;
}
//------------------------------------------------------------------------------
auto resource_manipulator_impl::_handle_resource_prepare(
  const message_context&,
  const stored_message& message) noexcept -> bool {
    _blobs.process_prepare(message);
    return true;
}
//------------------------------------------------------------------------------
auto resource_manipulator_impl::_handle_resource_available(
  const message_context&,
  const stored_message& message) noexcept -> bool {
    std::string url_str;
    if(default_deserialize(url_str, message.content())) [[likely]] {
        const url locator{std::move(url_str)};
        base.bus_node()
          .log_info("resource ${locator} is available at ${source}")
          .arg("source", message.source_id)
          .arg("locator", locator.str());
        signals.resource_appeared(message.source_id, locator);
    }
    return true;
}
//------------------------------------------------------------------------------
void resource_manipulator_impl::init(
  subscriber_discovery_signals& discovery,
  host_info_consumer_signals& host_info) noexcept {
    connect<&resource_manipulator_impl::_handle_alive>(
      this, discovery.reported_alive);
    connect<&resource_manipulator_impl::_handle_subscribed>(
      this, discovery.subscribed);
    connect<&resource_manipulator_impl::_handle_unsubscribed>(
      this, discovery.unsubscribed);
    connect<&resource_manipulator_impl::_handle_not_subscribed>(
      this, discovery.not_subscribed);
    connect<&resource_manipulator_impl::_handle_host_id_received>(
      this, host_info.host_id_received);
    connect<&resource_manipulator_impl::_handle_hostname_received>(
      this, host_info.hostname_received);
}
//------------------------------------------------------------------------------
void resource_manipulator_impl::add_methods() noexcept {
    base.add_method(
      this,
      message_map<
        "eagiRsrces",
        "hasResurce",
        &resource_manipulator_impl::_handle_has_resource>{});
    base.add_method(
      this,
      message_map<
        "eagiRsrces",
        "hasNotRsrc",
        &resource_manipulator_impl::_handle_has_not_resource>{});

    base.add_method(
      this,
      message_map<
        "eagiRsrces",
        "fragment",
        &resource_manipulator_impl::_handle_resource_fragment>{});
    base.add_method(
      this,
      message_map<
        "eagiRsrces",
        "notFound",
        &resource_manipulator_impl::_handle_resource_not_found>{});
    base.add_method(
      this,
      message_map<
        "eagiRsrces",
        "fragResend",
        &resource_manipulator_impl::_handle_resource_resend_request>{});
    base.add_method(
      this,
      message_map<
        "eagiRsrces",
        "blobPrpare",
        &resource_manipulator_impl::_handle_resource_prepare>{});
    base.add_method(
      this,
      message_map<
        "eagiRsrces",
        "available",
        &resource_manipulator_impl::_handle_resource_available>{});
}
//------------------------------------------------------------------------------
auto resource_manipulator_impl::update() noexcept -> work_done {
    some_true something_done{};
    something_done(_blobs.handle_complete() > 0);
    something_done(
      _blobs.update(base.bus_node().post_callable(), min_connection_data_size));

    if(_search_servers) {
        base.bus_node().query_subscribers_of(
          message_id{"eagiRsrces", "getContent"});
        something_done();
    }

    return something_done;
}
//------------------------------------------------------------------------------
auto resource_manipulator_impl::server_endpoint_id(const url& locator) noexcept
  -> endpoint_id_t {
    if(locator.has_scheme("eagimbe")) {
        if(const auto opt_id{
             from_string<identifier_t>(locator.host().or_default())}) {
            if(find(_server_endpoints, *opt_id)) {
                return *opt_id;
            }
        }
    } else if(locator.has_scheme("eagimbh")) {
        if(const auto hostname{locator.host()}) {
            if(const auto hfound{find(_hostname_to_endpoint, *hostname)}) {
                for(const auto endpoint_id : *hfound) {
                    if(find(_server_endpoints, endpoint_id)) {
                        return endpoint_id;
                    }
                }
            }
        }
    }
    return broadcast_endpoint_id();
}
//------------------------------------------------------------------------------
auto resource_manipulator_impl::search_resource(
  const endpoint_id_t endpoint_id,
  const url& locator) noexcept -> std::optional<message_sequence_t> {
    auto buffer = default_serialize_buffer_for(locator.str());

    if(const auto serialized{default_serialize(locator.str(), cover(buffer))})
      [[likely]] {
        const auto msg_id{message_id{"eagiRsrces", "qryResurce"}};
        message_view message{*serialized};
        message.set_target_id(endpoint_id);
        base.bus_node().set_next_sequence_id(msg_id, message);
        base.bus_node().post(msg_id, message);
        return {message.sequence_no};
    }
    return {};
}
//------------------------------------------------------------------------------
auto resource_manipulator_impl::query_resource_content(
  endpoint_id_t endpoint_id,
  const url& locator,
  shared_holder<target_blob_io> write_io,
  const message_priority priority,
  const std::chrono::seconds max_time) -> std::optional<message_sequence_t> {
    auto buffer = default_serialize_buffer_for(locator.str());

    if(endpoint_id == broadcast_endpoint_id()) {
        endpoint_id = server_endpoint_id(locator);
    }

    if(const auto serialized{default_serialize(locator.str(), cover(buffer))}) {
        const auto msg_id{message_id{"eagiRsrces", "getContent"}};
        message_view message{*serialized};
        message.set_target_id(endpoint_id);
        message.set_priority(priority);
        base.bus_node().set_next_sequence_id(msg_id, message);
        base.bus_node().post(msg_id, message);
        _blobs.expect_incoming(
          message_id{"eagiRsrces", "content"},
          endpoint_id,
          message.sequence_no,
          std::move(write_io),
          max_time);
        return {message.sequence_no};
    }
    return {};
}
//------------------------------------------------------------------------------
auto make_resource_manipulator_impl(
  subscriber& base,
  resource_manipulator_signals& sigs) -> unique_holder<resource_manipulator_intf> {
    return {hold<resource_manipulator_impl>, base, sigs};
}
//------------------------------------------------------------------------------
} // namespace eagine::msgbus
