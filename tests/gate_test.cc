/*
 * This file is open source software, licensed to you under the terms
 * of the Apache License, Version 2.0 (the "License").  See the NOTICE file
 * distributed with this work for additional information regarding copyright
 * ownership.  You may not use this file except in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
/*
 * Copyright (C) 2017 ScyllaDB
 */


#include "tests/test-utils.hh"

#include "core/gate.hh"
#include "core/sleep.hh"

using namespace seastar;
using namespace std::chrono_literals;

SEASTAR_TEST_CASE(test_gate_retries) {
    constexpr int retries = 2;
    auto g = std::make_unique<gate>();
    struct retry_policy {
        future<> prepare(gate& g) {
            return make_ready_future<>();
        }
        stdx::optional<future<int>> done(future<int>&& f) {
            return f.get0() < retries ? stdx::nullopt : stdx::make_optional(make_ready_future<int>(retries));
        }
        future<> backoff(gate& g) {
            return make_ready_future<>();
        }
    };
    auto f = with_gate(*g, retry_policy(), [i = 0] () mutable {
        ++i;
        return make_ready_future<int>(i);
    });
    return f.then([g = std::move(g)] (auto i) {
        BOOST_REQUIRE_EQUAL(retries, i);
    });
}

SEASTAR_TEST_CASE(test_gate_notifies_signal_target_notification) {
    auto p = std::make_unique<promise<>>();
    auto f = p->get_future();
    auto g = gate();
    auto st = g.signal_on_close([p = std::move(p)] () mutable {
        p->set_value();
    });
    return g.close().then([f = std::move(f), st = std::move(st)] () mutable {
        return std::move(f);
    });
}

SEASTAR_TEST_CASE(test_gate_signal_target_unregister) {
    auto signalled = make_lw_shared<bool>(false);
    auto g = gate();
    auto st = g.signal_on_close([signalled] {
        *signalled = true;
    });
    st = { };
    return g.close().then([signalled] {
        BOOST_REQUIRE_EQUAL(false, *signalled);
    });
}

SEASTAR_TEST_CASE(test_gate_sleep_abortable) {
    auto g = std::make_unique<gate>();
    auto f = sleep_abortable(100s, *g);
    return g->close().then([f = std::move(f)] () mutable {
        return std::move(f);
    }).then_wrapped([] (auto&& f) {
        try {
            f.get();
            BOOST_FAIL("should have failed");
        } catch (const sleep_aborted& e) {
            // expected
        }
    });
}