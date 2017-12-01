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

#include "scheduling.hh"
#include "util/noncopyable_function.hh"
#include <memory>

namespace seastar {

using task_func = noncopyable_function<void()>;

class task final {
    scheduling_group _sg;
    task_func _func;
public:
    explicit task(scheduling_group sg, task_func&& func) : _sg(sg), _func(std::move(func)) {}
    void run() noexcept { _func(); }
    scheduling_group group() const { return _sg; }
};

void schedule(task t);
void schedule_urgent(task t);


inline
task
make_task(task_func func) {
    return task(current_scheduling_group(), std::move(func));
}

inline
task
make_task(scheduling_group sg, task_func func) {
    return task(sg, std::move(func));
}

}
