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
 * Copyright (C) 2017 ScyllaDB.
 */

#pragma once

#include "util/noncopyable_function.hh"

#include <boost/intrusive/list.hpp>

#include <exception>
#include <experimental/optional>

namespace bi = boost::intrusive;

namespace seastar {

namespace stdx = std::experimental;

/// \addtogroup fiber-module
/// @{

/// Facility to communicate a cancellation request to a fiber.
/// Callbacks can be registered with the \c abort_source, which are called
/// atomically with a call to request_abort().
class abort_source {
    //FIXME: Add noexcept in function type when we move to C++17
    using subscription_callback_type = noncopyable_function<void()>;

public:
    /// Represents a handle to the callback registered by a given fiber. Ending the
    /// lifetime of the \c subscription will unregister the callback, if it hasn't
    /// been invoked yet.
    class subscription : public bi::list_base_hook<bi::link_mode<bi::auto_unlink>> {
        friend class abort_source;

        abort_source* _as;
        subscription_callback_type _target;

        explicit subscription(abort_source& as, subscription_callback_type target)
                : _as(&as)
                , _target(std::move(target)) {
            as._subscriptions->push_back(*this);
        }

        void on_abort() {
            _target();
        }

    public:
        subscription() = default;

        subscription(subscription&& other) noexcept(std::is_nothrow_move_constructible<subscription_callback_type>::value)
                : _as(other._as)
                , _target(std::move(other._target)) {
            subscription_list_type::node_algorithms::swap_nodes(other.this_ptr(), this_ptr());
        }

        subscription& operator=(subscription&& other) noexcept(std::is_nothrow_move_assignable<subscription_callback_type>::value) {
            if (this != &other) {
                _target = std::move(other._target);
                _as = other._as;
                subscription_list_type::node_algorithms::unlink(this_ptr());
                subscription_list_type::node_algorithms::swap_nodes(other.this_ptr(), this_ptr());
            }
            return *this;
        }
    };

private:
    using subscription_list_type = bi::list<subscription, bi::constant_time_size<false>>;
    stdx::optional<subscription_list_type> _subscriptions = subscription_list_type();

public:
    /// Delays the invocation of the callback \c f until \ref request_abort() is called.
    /// Can only be called if !\ref abort_requested returns false.
    /// \returns a \ref subscription that can be used to control the lifetime of the callback \c f.
    subscription subscribe(subscription_callback_type f) noexcept(std::is_nothrow_move_constructible<subscription_callback_type>::value) {
        return subscription(*this, std::move(f));
    }

    /// Requests that the target operation be aborted. Current subscriptions
    /// are invoked inline with this call, and no new ones can be registered.
    void request_abort() {
        _subscriptions->clear_and_dispose([] (subscription* s) { s->on_abort(); });
        _subscriptions = { };
    }

    /// Returns whether an abort has been requested.
    bool abort_requested() noexcept {
        return !_subscriptions;
    }
};

/// @}

}