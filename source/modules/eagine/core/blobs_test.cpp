/// @file
///
/// Copyright Matus Chochlik.
/// Distributed under the Boost Software License, Version 1.0.
/// See accompanying file LICENSE_1_0.txt or copy at
///  http://www.boost.org/LICENSE_1_0.txt
///

#include <eagine/testing/unit_begin_ctx.hpp>
import <memory>;
import <type_traits>;
import eagine.core;
import eagine.msgbus.core;
//------------------------------------------------------------------------------
// round-trip zeroes
//------------------------------------------------------------------------------
class zeroes_source_blob_io final : public eagine::msgbus::source_blob_io {
public:
    zeroes_source_blob_io(const eagine::span_size_t size) noexcept
      : _size{size} {}

    auto total_size() noexcept -> eagine::span_size_t final {
        return _size;
    }

    auto fetch_fragment(
      const eagine::span_size_t offs,
      eagine::memory::block dst) noexcept -> eagine::span_size_t final {
        using namespace eagine::memory;
        return zero(head(dst, _size - offs)).size();
    }

private:
    eagine::span_size_t _size;
};
//------------------------------------------------------------------------------
class zeroes_target_blob_io final : public eagine::msgbus::target_blob_io {
public:
    zeroes_target_blob_io(
      eagitest::case_& test,
      eagitest::track& trck,
      const eagine::span_size_t size,
      bool& done) noexcept
      : _test{test}
      , _trck{trck}
      , _expected_size{size}
      , _done{done} {}

    void handle_finished(
      const eagine::message_id msg_id,
      [[maybe_unused]] const eagine::msgbus::message_age msg_age,
      [[maybe_unused]] const eagine::msgbus::message_info& message,
      [[maybe_unused]] const eagine::msgbus::blob_info& info) noexcept final {

        _test.check(
          msg_id.class_() == eagine::identifier{"test"}, "message id");
        _done = true;
        _trck.passed_part(2);
    }

    void handle_cancelled() noexcept final {
        _test.fail("blob cancelled");
        _done = true;
    }

    auto store_fragment(
      [[maybe_unused]] const eagine::span_size_t offs,
      [[maybe_unused]] eagine::memory::const_block data,
      [[maybe_unused]] const eagine::msgbus::blob_info& info) noexcept
      -> bool final {

        _test.check(offs >= 0, "offset ok 1");
        _test.check(offs < _expected_size, "offset ok 2");
        for(const auto b : data) {
            _test.check_equal(b, eagine::byte{0}, "is zero");
        }
        _done_size += data.size();
        _trck.passed_part(3);
        return true;
    }

    auto check_stored(
      [[maybe_unused]] const eagine::span_size_t offs,
      [[maybe_unused]] eagine::memory::const_block data) noexcept
      -> bool final {

        _test.check(offs >= 0, "offset ok 3");
        _test.check(offs < _expected_size, "offset ok 4");
        for(const auto b : data) {
            _test.check_equal(b, eagine::byte{0}, "is zero");
        }
        _trck.passed_part(4);
        return true;
    }

private:
    eagitest::case_& _test;
    eagitest::track& _trck;
    eagine::span_size_t _expected_size;
    eagine::span_size_t _done_size{0};
    bool& _done;
};
//------------------------------------------------------------------------------
void blobs_roundtrip_zeroes_single(unsigned r, auto& s) {
    eagitest::case_ test{s, 1, "round-trip zeroes"};
    eagitest::track trck{test, 1, 4};

    const eagine::message_id test_msg_id{"test", eagine::random_identifier()};
    const eagine::message_id send_msg_id{"test", "send"};
    const eagine::message_id resend_msg_id{"test", "resend"};
    eagine::msgbus::blob_manipulator sender{
      s.context(), send_msg_id, resend_msg_id};
    eagine::msgbus::blob_manipulator receiver{
      s.context(), send_msg_id, resend_msg_id};

    auto send_s2r = [&](
                      const eagine::message_id msg_id,
                      const eagine::msgbus::message_view& message) -> bool {
        test.check(msg_id == send_msg_id, "message id");

        receiver.process_incoming(message);

        trck.passed_part(1);
        return true;
    };
    const eagine::msgbus::blob_manipulator::send_handler handler_s2r{
      eagine::construct_from, send_s2r};

    auto send_r2s = [&](
                      const eagine::message_id,
                      const eagine::msgbus::message_view&) -> bool {
        return true;
    };
    const eagine::msgbus::blob_manipulator::send_handler handler_r2s{
      eagine::construct_from, send_r2s};

    sender.push_outgoing(
      test_msg_id,
      0,
      1,
      eagine::msgbus::blob_id_t(r),
      std::make_unique<zeroes_source_blob_io>(4 * 1024 * 1024),
      std::chrono::hours{1},
      eagine::msgbus::message_priority::normal);

    bool done{false};

    receiver.expect_incoming(
      test_msg_id,
      0,
      eagine::msgbus::blob_id_t(r),
      std::make_unique<zeroes_target_blob_io>(
        test, trck, 4 * 1024 * 1024, done),
      std::chrono::hours{1});

    while(not done) {
        sender.update(handler_s2r);
        sender.process_outgoing(handler_s2r, 2048, 2);
        receiver.update(handler_r2s);
        receiver.handle_complete();
    }
}
//------------------------------------------------------------------------------
// main
//------------------------------------------------------------------------------
auto test_main(eagine::test_ctx& ctx) -> int {
    eagitest::ctx_suite test{ctx, "blobs", 1};
    test.repeat(8, blobs_roundtrip_zeroes_single);
    return test.exit_code();
}
//------------------------------------------------------------------------------
auto main(int argc, const char** argv) -> int {
    return eagine::test_main_impl(argc, argv, test_main);
}
//------------------------------------------------------------------------------
#include <eagine/testing/unit_end_ctx.hpp>
