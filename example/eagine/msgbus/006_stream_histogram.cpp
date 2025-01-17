/// @example eagine/msgbus/006_stream_histogram.cpp
///
/// Copyright Matus Chochlik.
/// Distributed under the Boost Software License, Version 1.0.
/// See accompanying file LICENSE_1_0.txt or copy at
/// https://www.boost.org/LICENSE_1_0.txt
///
import eagine.core;
import eagine.sslplus;
import eagine.msgbus;
import std;

namespace eagine {

auto main(main_ctx& ctx) -> int {
    enable_message_bus(ctx);

    timeout idle_too_long{std::chrono::seconds{30}};

    msgbus::endpoint bus{"Example", ctx};
    msgbus::resource_data_consumer_node node{bus};
    msgbus::setup_connectors(ctx, node);

    span_size_t max_count{0};
    span_size_t streamed_bytes{0};
    std::array<span_size_t, 256> byte_counts{};
    zero(cover(byte_counts));

    auto consume = [&](const msgbus::blob_stream_chunk& chunk) {
        for(const auto& blk : chunk.data) {
            for(auto b : blk) {
                max_count = math::maximum(max_count, ++byte_counts[std_size(b)]);
                if((++streamed_bytes % (4 * 1024 * 1024)) == 0) {
                    ctx.log()
                      .info("streamed ${count}")
                      .tag("strmdBytes")
                      .arg("count", "ByteSize", streamed_bytes);
                }
            }
        }
    };

    node.blob_stream_data_appended.connect({construct_from, consume});

    auto enqueue = [&](url locator, bool chunks) {
        if(locator) {
            if(chunks) {
                node.fetch_resource_chunks(
                  {.locator = std::move(locator),
                   .max_time = std::chrono::hours{1},
                   .priority = msgbus::message_priority::high},
                  ctx.default_chunk_size());
            } else {
                node.stream_resource(
                  {.locator = std::move(locator),
                   .max_time = std::chrono::hours{1},
                   .priority = msgbus::message_priority::high});
            }
        }
    };

    for(auto& arg : ctx.args()) {
        enqueue(url(arg.get_string()), false);
    }
    if(not node.has_pending_resources()) {
        enqueue(url("eagires:///ones?count=134217728"), false);
        enqueue(url("eagires:///zeroes?count=134217728"), true);
        enqueue(url("eagires:///random?count=1073741824"), false);
        enqueue(url("eagires:///random?count=134217728"), true);
        enqueue(url("eagires:///ownSource"), true);
    }

    const auto is_done = [&] {
        if(idle_too_long or not node.has_pending_resources()) {
            return true;
        }
        return false;
    };

    while(not is_done()) {
        if(node.update_and_process_all()) {
            idle_too_long.reset();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    ctx.log()
      .info("blob byte counts")
      .tag("blobHstgrm")
      .arg_func([&](logger_backend& backend) {
          for(const auto i : integer_range(std_size(256))) {
              if(byte_counts[i]) {
                  backend.add_float(
                    byte_to_identifier(byte(i)),
                    "Histogram",
                    float(0),
                    float(byte_counts[i]),
                    float(max_count));
              }
          }
      });

    return 0;
}

} // namespace eagine

auto main(int argc, const char** argv) -> int {
    eagine::main_ctx_options options;
    options.app_id = "RsrcExmple";
    return eagine::main_impl(argc, argv, options, eagine::main);
}
