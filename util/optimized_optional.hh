/*
 * Copyright (C) 2016 ScyllaDB
 */

/*
 * This file is part of Scylla.
 *
 * Scylla is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Scylla is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Scylla.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "util/gcc6-concepts.hh"

#include <experimental/optional>
#include <experimental/type_traits>
#include <iostream>

namespace seastar {

namespace stdx = std::experimental;

GCC6_CONCEPT(

template<typename T>
concept bool OptimizableOptional() {
    return stdx::is_default_constructible_v<T>
        && stdx::is_nothrow_move_assignable_v<T>
        && requires(const T& obj) {
            { bool(obj) } noexcept;
        };
}

)

/// \c optimized_optional<> is intended mainly for use with classes that store
/// their data externally and expect pointer to this data to be always non-null.
/// In such case there is no real need for another flag signifying whether
/// the optional is engaged.
template<typename T>
class optimized_optional {
    T _object;
public:
    optimized_optional() = default;
    optimized_optional(std::experimental::nullopt_t) noexcept { }
    optimized_optional(const T& obj) : _object(obj) { }
    optimized_optional(T&& obj) noexcept : _object(std::move(obj)) { }
    optimized_optional(std::experimental::optional<T>&& obj) noexcept {
        if (obj) {
            _object = std::move(*obj);
        }
    }
    optimized_optional(const optimized_optional&) = default;
    optimized_optional(optimized_optional&&) = default;

    optimized_optional& operator=(std::experimental::nullopt_t) noexcept {
        _object = T();
        return *this;
    }
    template<typename U>
    std::enable_if_t<std::is_same<std::decay_t<U>, T>::value, optimized_optional&>
    operator=(U&& obj) noexcept {
        _object = std::forward<U>(obj);
        return *this;
    }
    optimized_optional& operator=(const optimized_optional&) = default;
    optimized_optional& operator=(optimized_optional&&) = default;

    explicit operator bool() const noexcept {
        return bool(_object);
    }

    T* operator->() noexcept { return &_object; }
    const T* operator->() const noexcept { return &_object; }

    T& operator*() noexcept { return _object; }
    const T& operator*() const noexcept { return _object; }

    bool operator==(const optimized_optional& other) const {
        return _object == other._object;
    }
    bool operator!=(const optimized_optional& other) const {
        return _object != other._object;
    }
    friend std::ostream& operator<<(std::ostream& out, const optimized_optional& opt) {
        if (!opt) {
            return out << "null";
        }
        return out << *opt;
    }
};

}