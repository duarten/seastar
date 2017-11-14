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

class task {
    scheduling_group _sg;
public:
    explicit task(scheduling_group sg = current_scheduling_group()) : _sg(sg) {}
    virtual ~task() noexcept {}
    virtual void run() noexcept = 0;
    scheduling_group group() const { return _sg; }
};

void schedule(std::unique_ptr<task> t);
void schedule_urgent(std::unique_ptr<task> t);

class lambda_task final : public task {
    task_func _func;
public:
    lambda_task(scheduling_group sg, task_fund&& func) : task(sg), _func(std::move(func)) {}
    virtual void run() noexcept override { _func(); }
};

inline
std::unique_ptr<task>
make_task(task_func func) {
    return std::make_unique<lambda_task>(current_scheduling_group(), std::move(func));
}

inline
std::unique_ptr<task>
make_task(scheduling_group sg, task_func func) {
    return std::make_unique<lambda_task>(sg, std::move(func));
}

}
