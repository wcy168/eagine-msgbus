/// @file
///
/// Copyright Matus Chochlik.
/// Distributed under the Boost Software License, Version 1.0.
/// See accompanying file LICENSE_1_0.txt or copy at
///  http://www.boost.org/LICENSE_1_0.txt
///

#include <eagine/testing/unit_begin_ctx.hpp>
import eagine.core;
import eagine.msgbus.core;
import eagine.msgbus.services;
import std;
//------------------------------------------------------------------------------
template <typename Base = eagine::msgbus::subscriber>
class test_pong : public Base {

protected:
    using Base::Base;

    void add_methods() noexcept {
        Base::add_methods();
        Base::add_method(
          this,
          eagine::msgbus::
            message_map<"eagiTest", "ping", &test_pong::_handle_ping>{});
    }

private:
    auto _handle_ping(
      const eagine::msgbus::message_context&,
      const eagine::msgbus::stored_message& message) noexcept -> bool {
        this->bus_node().respond_to(
          message, eagine::message_id{"eagiTest", "pong"}, {});
        return true;
    }
};
//------------------------------------------------------------------------------
template <typename Base = eagine::msgbus::subscriber>
class test_ping : public Base {
public:
    void assign_target(eagine::identifier_t id) noexcept {
        _target = id;
    }

protected:
    using Base::Base;

    void add_methods() noexcept {
        Base::add_methods();
        Base::add_method(
          this,
          eagine::msgbus::
            message_map<"eagiTest", "pong", &test_ping::_handle_pong>{});
    }

    auto update() -> eagine::work_done {
        eagine::some_true something_done{Base::update()};
        if(eagine::msgbus::is_valid_endpoint_id(_target)) {
            if(_ping_time.is_expired()) {
                eagine::msgbus::message_view ping_msg;
                ping_msg.set_target_id(_target);
                ping_msg.set_sequence_no(_seq_id);
                this->bus_node().post({"eagiTest", "ping"}, ping_msg);
                _ping_time.reset();
                something_done();
            }
        }
        return something_done;
    }

private:
    auto _handle_pong(
      const eagine::msgbus::message_context&,
      const eagine::msgbus::stored_message&) noexcept -> bool {
        ++_rcvd;
        return true;
    }

    int _rcvd{0};
    eagine::msgbus::message_sequence_t _seq_id{0};
    eagine::timeout _ping_time{std::chrono::milliseconds{1}};
    eagine::identifier_t _target{eagine::msgbus::invalid_endpoint_id()};
};
//------------------------------------------------------------------------------
// test 1
//------------------------------------------------------------------------------
void discovery_1(auto& s) {
    eagitest::case_ test{s, 1, "1"};
    eagitest::track trck{test, 0, 2};
    auto& ctx{s.context()};
    eagine::msgbus::registry the_reg{ctx};

    auto& observer = the_reg.emplace<eagine::msgbus::service_composition<
      eagine::msgbus::subscriber_discovery<>>>("Observer");

    if(the_reg.wait_for_id_of(std::chrono::seconds{30}, observer)) {
        auto& pinger =
          the_reg.emplace<eagine::msgbus::service_composition<test_ping<>>>(
            "TestPing");
        auto& ponger =
          the_reg.emplace<eagine::msgbus::service_composition<test_pong<>>>(
            "TestPong");

        bool found_pinger{false};
        bool found_ponger{false};
        bool pinger_alive{false};
        bool ponger_alive{false};

        const auto discovered_all{[&] {
            return found_pinger and found_ponger and pinger_alive and
                   ponger_alive;
        }};

        const auto handle_alive{
          [&](const eagine::msgbus::subscriber_info& sub) {
              if(pinger.get_id() == sub.endpoint_id) {
                  pinger_alive = true;
              }
              if(ponger.get_id() == sub.endpoint_id) {
                  ponger_alive = true;
              }
              trck.checkpoint(1);
          }};
        observer.reported_alive.connect({eagine::construct_from, handle_alive});

        const auto handle_subscribed{
          [&](
            const eagine::msgbus::subscriber_info& sub,
            const eagine::message_id msg_id) {
              if(msg_id == eagine::message_id{"eagiTest", "pong"}) {
                  test.check_equal(
                    sub.endpoint_id,
                    pinger.get_id().value_or(
                      eagine::msgbus::invalid_endpoint_id()),
                    "pinger id");
                  found_pinger = true;
              }
              if(msg_id == eagine::message_id{"eagiTest", "ping"}) {
                  test.check_equal(
                    sub.endpoint_id,
                    ponger.get_id().value_or(
                      eagine::msgbus::invalid_endpoint_id()),
                    "ponger id");
                  found_ponger = true;
              }
              trck.checkpoint(2);
          }};
        observer.subscribed.connect(
          {eagine::construct_from, handle_subscribed});

        if(the_reg.wait_for_id_of(std::chrono::seconds{30}, pinger, ponger)) {
            pinger.assign_target(ponger.bus_node().get_id());
            eagine::timeout discovery_time{std::chrono::minutes{1}};
            while(not discovered_all()) {
                if(discovery_time.is_expired()) {
                    test.fail("discovery timeout");
                    break;
                }
                the_reg.update_and_process();
            }
        } else {
            test.fail("get id ping/pong");
        }
    } else {
        test.fail("get id observer");
    }

    the_reg.finish();
}
//------------------------------------------------------------------------------
// main
//------------------------------------------------------------------------------
auto test_main(eagine::test_ctx& ctx) -> int {
    enable_message_bus(ctx);
    ctx.preinitialize();

    eagitest::ctx_suite test{ctx, "discovery", 1};
    test.once(discovery_1);
    return test.exit_code();
}
//------------------------------------------------------------------------------
auto main(int argc, const char** argv) -> int {
    return eagine::test_main_impl(argc, argv, test_main);
}
//------------------------------------------------------------------------------
#include <eagine/testing/unit_end_ctx.hpp>
