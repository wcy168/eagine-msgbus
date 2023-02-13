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
import <chrono>;
//------------------------------------------------------------------------------
// test 1
//------------------------------------------------------------------------------
void host_info_1(auto& s) {
    eagitest::case_ test{s, 1, "1"};
    eagitest::track trck{test, 0, 2};
    auto& ctx{s.context()};
    eagine::msgbus::registry the_reg{ctx};

    auto& provider = the_reg.emplace<
      eagine::msgbus::service_composition<eagine::msgbus::host_info_provider<>>>(
      "Provider");
    auto& consumer = the_reg.emplace<
      eagine::msgbus::service_composition<eagine::msgbus::host_info_consumer<>>>(
      "Consumer");

    if(the_reg.wait_for_id_of(std::chrono::seconds{30}, provider, consumer)) {
        bool has_hostname{false};

        const auto received_all{[&] {
            return has_hostname;
        }};

        const auto handle_hostname{
          [&](
            const eagine::msgbus::result_context& rc,
            const eagine::valid_if_not_empty<std::string>& name) {
              has_hostname = name.has_value();
              test.check(name.has_value(), "has host name");
              test.check(provider.get_id() == rc.source_id(), "from provider");
              trck.checkpoint(1);
          }};

        consumer.hostname_received.connect(
          {eagine::construct_from, handle_hostname});

        eagine::timeout query_timeout{std::chrono::seconds{5}, eagine::nothing};
        eagine::timeout receive_timeout{std::chrono::seconds{30}};
        while(not received_all()) {
            if(query_timeout.is_expired()) {
                consumer.query_hostname(provider.get_id().value());
                query_timeout.reset();
                trck.checkpoint(2);
            }
            if(receive_timeout.is_expired()) {
                test.fail("receive timeout");
                break;
            }
            the_reg.update_all();
        }
    }

    the_reg.finish();
}
//------------------------------------------------------------------------------
// main
//------------------------------------------------------------------------------
auto test_main(eagine::test_ctx& ctx) -> int {
    enable_message_bus(ctx);
    ctx.preinitialize();

    eagitest::ctx_suite test{ctx, "host info", 1};
    test.once(host_info_1);
    return test.exit_code();
}
//------------------------------------------------------------------------------
auto main(int argc, const char** argv) -> int {
    return eagine::test_main_impl(argc, argv, test_main);
}
//------------------------------------------------------------------------------
#include <eagine/testing/unit_end_ctx.hpp>
