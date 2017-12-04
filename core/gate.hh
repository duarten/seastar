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
 * Copyright 2014 Cloudius Systems
 */

#pragma once

#include "future.hh"
#include <boost/intrusive/list.hpp>
#include <boost/range/algorithm/for_each.hpp>
#include <experimental/optional>
#include <exception>

namespace bi = boost::intrusive;

namespace seastar {

/// \addtogroup fiber-module
/// @{

namespace stdx = std::experimental;

/// Exception thrown when a \ref gate object has been closed
/// by the \ref gate::close() method.
class gate_closed_exception : public std::exception {
public:
    virtual const char* what() const noexcept override {
        return "gate closed";
    }
};

/// Facility to stop new requests, and to tell when existing requests are done.
///
/// When stopping a service that serves asynchronous requests, we are faced with
/// two problems: preventing new requests from coming in, and knowing when existing
/// requests have completed.  The \c gate class provides a solution.
class gate {
public:
    class signal_target : public bi::list_base_hook<bi::link_mode<bi::auto_unlink>> {
        friend class gate;

        gate* _g;
        noncopyable_function<void ()> _target;

        void on_close() {
            _target();
        }

        explicit signal_target(gate& g, noncopyable_function<void ()> target)
                : _g(&g)
                ,  _target(std::move(target)) {
            g._to_signal.push_back(*this);
        }

    public:
        signal_target() = default;

        signal_target(signal_target&& other)
                : _g(other._g)
                , _target(std::move(other._target)) {
            if (other.is_linked()) {
                signal_target_list_type::node_algorithms::init(this_ptr());
                signal_target_list_type::node_algorithms::swap_nodes(other.this_ptr(), this_ptr());
            }
        }

        signal_target& operator=(signal_target&& other) {
            if (this != &other) {
                this->~signal_target();
                new (this) signal_target(std::move(other));
            }
            return *this;
        }
    };
 private:
    size_t _count = 0;
    stdx::optional<promise<>> _stopped;
    using signal_target_list_type = bi::list<signal_target, bi::constant_time_size<false>>;
    signal_target_list_type _to_signal;
public:
    /// Registers an in-progress request.
    ///
    /// If the gate is not closed, the request is registered.  Otherwise,
    /// a \ref gate_closed_exception is thrown.
    void enter() {
        if (_stopped) {
            throw gate_closed_exception();
        }
        ++_count;
    }
    /// Unregisters an in-progress request.
    ///
    /// If the gate is closed, and there are no more in-progress requests,
    /// the \ref closed() promise will be fulfilled.
    void leave() {
        --_count;
        if (!_count && _stopped) {
            _stopped->set_value();
        }
    }
    /// Potentially stop an in-progress request.
    ///
    /// If the gate is already closed, a \ref gate_closed_exception is thrown.
    /// By using \ref enter() and \ref leave(), the program can ensure that
    /// no further requests are serviced. However, long-running requests may
    /// continue to run. The check() method allows such a long operation to
    /// voluntarily stop itself after the gate is closed, by making calls to
    /// check() in appropriate places. check() with throw an exception and
    /// bail out of the long-running code if the gate is closed.
    void check() {
        if (_stopped) {
            throw gate_closed_exception();
        }
    }
    /// Closes the gate.
    ///
    /// Future calls to \ref enter() will fail with an exception, and when
    /// all current requests call \ref leave(), the returned future will be
    /// made ready.
    future<> close() {
        assert(!_stopped && "seastar::gate::close() cannot be called more than once");
        _stopped = stdx::make_optional(promise<>());
        if (!_count) {
            _stopped->set_value();
        }
        boost::range::for_each(_to_signal, std::mem_fn(&signal_target::on_close));
        return _stopped->get_future();
    }

    /// Returns a current number of registered in-progress requests.
    size_t get_count() const {
        return _count;
    }

    /// Returns whether the gate is closed.
    bool is_closed() const {
        return bool(_stopped);
    }

    /// Register callback to be invoked when the gate is closed.
    ///
    /// Returns a handle to the registration, which ensure the
    /// callback is unregistered when the handle's lifetime ends.
    signal_target signal_on_close(noncopyable_function<void ()> target) {
        return signal_target(*this, std::move(target));
    }
};

/// Executes the function \c func making sure the gate \c g is properly entered
/// and later on, properly left.
///
/// \param func function to be executed
/// \param g the gate. Caller must make sure that it outlives this function.
/// \returns whatever \c func returns
///
/// \relates gate
template <typename Func>
inline
auto
with_gate(gate& g, Func&& func) {
    g.enter();
    return func().finally([&g] { g.leave(); });
}
/// @}

}
