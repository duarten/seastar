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
 * Copyright (C) 2015 Cloudius Systems, Ltd.
 */

#pragma once

#include <chrono>
#include <functional>

#include "core/gate.hh"
#include "core/shared_ptr.hh"
#include "core/reactor.hh"
#include "core/future.hh"

namespace seastar {

/// \file

/// Returns a future which completes after a specified time has elapsed.
///
/// \param dur minimum amount of time before the returned future becomes
///            ready.
/// \return A \ref future which becomes ready when the sleep duration elapses.
template <typename Clock = steady_clock_type, typename Rep, typename Period>
future<> sleep(std::chrono::duration<Rep, Period> dur) {
    struct sleeper {
        promise<> done;
        timer<Clock> tmr;
        sleeper(std::chrono::duration<Rep, Period> dur)
            : tmr([this] { done.set_value(); })
        {
            tmr.arm(dur);
        }
    };
    sleeper *s = new sleeper(dur);
    future<> fut = s->done.get_future();
    return fut.then([s] { delete s; });
}

/// exception that is thrown when application is in process of been stopped
class sleep_aborted : public std::exception {
public:
    /// Reports the exception reason.
    virtual const char* what() const noexcept {
        return "Sleep is aborted";
    }
};

/// Returns a future which completes after a specified time has elapsed
/// or throws \ref sleep_aborted exception if application is aborted
///
/// \param dur minimum amount of time before the returned future becomes
///            ready.
/// \return A \ref future which becomes ready when the sleep duration elapses.
template <typename Rep, typename Period>
future<> sleep_abortable(std::chrono::duration<Rep, Period> dur) {
    return engine().wait_for_stop(dur).then([] {
        throw sleep_aborted();
    }).handle_exception([] (std::exception_ptr ep) {
        try {
            std::rethrow_exception(ep);
        } catch(condition_variable_timed_out&) {};
    });
}

/// Returns a future which completes after a specified time has elapsed
/// or throws \ref sleep_aborted exception if the sleep is aborted.
///
/// \param dur minimum amount of time before the returned future becomes
///            ready.
/// \param g the gate that, upon being closes, notifies that the sleep
///            should be aborted.
/// \return A \ref future which becomes ready when the sleep duration elapses.
template <typename Clock = steady_clock_type, typename Rep, typename Period>
future<> sleep_abortable(std::chrono::duration<Rep, Period> dur, gate& g) {
    if (g.is_closed()) {
        return make_exception_future<>(sleep_aborted());
    }

    struct sleeper {
        promise<> done;
        timer<Clock> tmr;
        gate::signal_target st;

        sleeper(std::chrono::duration<Rep, Period> dur, gate& g)
                : tmr([this, &g] {
                    if (!g.is_closed()) {
                        done.set_value();
                    }
                  })
                , st(g.signal_on_close([this] {
                    if (tmr.cancel()) {
                       done.set_exception(sleep_aborted());
                    }
                  })) {
            tmr.arm(dur);
        }
    };
    auto s = std::make_unique<sleeper>(dur, g);
    return s->done.get_future().finally([s = std::move(s)] { });
}

}
