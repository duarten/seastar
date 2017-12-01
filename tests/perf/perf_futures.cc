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

#include "core/app-template.hh"
#include "core/future.hh"
#include "core/future-util.hh"

using namespace seastar;
using namespace std::chrono_literals;

using small_type = std::array<uint64_t, 2>;
using large_type = std::array<uint64_t, 12>;

int main(int ac, char** av) {
    app_template at;
    return at.run(ac, av, [&at] {
        return do_with(0, std::chrono::high_resolution_clock::now(), [] (int& ops, auto& start) {
            return repeat([&] {
                return later().then([&, s = small_type()] () mutable {
                    return later().then([&, l = large_type()] () mutable {
                        s[++ops % s.size()]++;
                        l[ops % l.size()]++;

                        auto stop = std::chrono::high_resolution_clock::now();
                        if (stop - start >= 10s) {
                            std::cout << ops << "ops\n";
                            return make_ready_future<stop_iteration>(true);
                        }
                        return make_ready_future<stop_iteration>(false);
                    });
                });
            });
        });
    });
}
