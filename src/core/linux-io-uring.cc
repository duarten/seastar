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
 * Copyright (C) 2019 Vectorized.io
 */

#include <seastar/core/linux-io-uring.hh>
#include <seastar/core/posix.hh>

#include <atomic>
#include <algorithm>
#include <errno.h>
#include <sys/mman.h>
#include <sys/syscall.h>

namespace seastar {

namespace internal {

namespace linux_abi {

int io_uring_setup(uint32_t entries, io_uring_params* params) {
    return ::syscall(SYS_io_uring_setup, entries, params);
}

int io_uring_enter(unsigned fd, unsigned to_submit, unsigned min_completed, io_uring_enter_flags flags, sigset_t* sig) {
	return ::syscall(SYS_io_uring_enter, fd, to_submit, min_completed, flags, sig);
}

int io_uring_register(int fd, io_uring_register_opcode opcode, void* arg, unsigned nr_args) {
	return ::syscall(SYS_io_uring_register, fd, opcode, arg, nr_args);
}

} // namespace linux_abi

static void io_uring_queue_mmap(int fd, linux_abi::io_uring_params& params, io_uring& ring) {
	auto do_mmap = [&fd] (std::size_t size, uint64_t magic) noexcept {
		return mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, magic);
	};
    ring.sq.ring_size = params.sq_off.index_array + params.sq_entries * sizeof(uint32_t);
	auto ptr = do_mmap(ring.sq.ring_size, linux_abi::IORING_OFF_SQ_RING);
	throw_system_error_on(ptr == MAP_FAILED, "mmap");
	auto unmap_sq_ring = defer([ptr, &ring] {
		munmap(ptr, ring.sq.ring_size);
	});
	ring.sq.head = static_cast<std::atomic<uint32_t>*>(ptr + params.sq_off.head);
	ring.sq.tail = static_cast<std::atomic<uint32_t>*>(ptr + params.sq_off.tail);
	ring.sq.ring_mask = *static_cast<uint32_t*>(ptr + params.sq_off.ring_mask);
	ring.sq.flags = static_cast<uint32_t*>(ptr + params.sq_off.flags);
	ring.sq.dropped = static_cast<uint32_t*>(ptr + params.sq_off.dropped);
	ring.sq.index_array = static_cast<uint32_t*>(ptr + params.sq_off.array);

    ptr = do_mmap(params.sq_entries * sizeof(linux_abi::io_uring_sqe), linux_abi::IORING_OFF_SQES);
	ring.sq.sqes = static_cast<linux_abi::io_uring_sqe*>(ptr);
	throw_system_error_on(ring.sq.sqes == MAP_FAILED, "mmap");
	auto unmap_sqes = defer([&ring, &params] {
		munmap(ring.sq.sqes, params.sq_entries * sizeof(linux_abi::io_uring_sqe));
	});
	ring.sq.sqe_head = 0;
	ring.sq.sqe_tail = 0;
	ring.sq.pending = false;

	ring.cq.ring_size = params.cq_off.cqes + params.cq_entries * sizeof(linux_abi::io_uring_cqe);
	ptr = do_mmap(ring.cq.ring_size, linux_abi::IORING_OFF_CQ_RING);
	throw_system_error_on(ptr == MAP_FAILED, "mmap");
	ring.cq.head = static_cast<std::atomic<uint32_t>*>(ptr + params.cq_off.head);
	ring.cq.tail = static_cast<std::atomic<uint32_t>*>(ptr + params.cq_off.tail);
	ring.cq.ring_mask = *static_cast<uint32_t*>(ptr + params.cq_off.ring_mask);
	ring.cq.overflow = static_cast<uint32_t*>(ptr + params.cq_off.overflow);
	ring.cq.cqes = static_cast<linux_abi::io_uring_cqe*>(ptr + params.cq_off.cqes);

	unmap_sq_ring.cancel();
	unmap_sqes.cancel();
}

void io_uring_queue_init(uint32_t entries, io_uring& ring, linux_abi::io_uring_setup_flags flags) {
    assert((flags & linux_abi::io_uring_setup_flags::SQPOLL) == 0);
    linux_abi::io_uring_params p;
	p.flags = flags;
	auto fd = linux_abi::io_uring_setup(entries, &p);
	throw_system_error_on(fd < 0, "io_uring_setup");
	io_uring_queue_mmap(fd, p, ring);
}

void io_uring_queue_exit(io_uring& ring) {
	munmap(ring.sq.sqes, ring.sq.ring_size * sizeof(linux_abi::io_uring_sqe));
	munmap(ring.sq.head, ring.cq.ring_size);
	munmap(ring.cq.head, ring.cq.ring_size);
	::close(ring.ring_fd);
}

static void do_io_uring_enter(io_uring& ring, unsigned submitted) {
	auto ret = io_uring_enter(ring.ring_fd, submitted, 0,
			linux_abi::io_uring_enter_flags::GETEVENTS, nullptr);
	if (__builtin_expect(ret < 0, false)) {
		ring.sq.pending = true;
		throw std::system_error(errno, std::system_category(), "io_uring_enter");
	}
	ring.sq.pending = false;
}

unsigned io_uring_submit(io_uring& ring) {
	auto tail = ring.sq.tail->load(std::memory_order_relaxed);
	if (__builtin_expect(ring.sq.pending, false)) {
		auto submitted = tail - ring.sq.head->load(std::memory_order_acquire);
		if (submitted) {
			// To avoid complicating index management, finish submitting
			// pending entries which may remain in the kernel-visible queue
			// due a failed, previous call to io_uring_enter().
			do_io_uring_enter(ring, submitted);
			return;
		}
		ring.sq.pending = false;
	}
	auto tail_next = tail;
	auto mask = ring.sq.ring_mask;
	// Since we are well-behaved, we can avoid loading the
	// kernel-owned `ring.sq.head` variable, so we save an
	// atomic instruction.
	while (ring.sq.sqe_head < ring.sq.sqe_tail) {
		ring.sq.index_array[tail_next & mask] = ring.sq.sqe_head & mask;
		ring.sq.sqe_head++;
		tail_next++;
	}
	auto submitted = tail_next - tail;
	if (submitted) {
		// Publish the tail only after the SQE writes and
		// order with the kernel-side acquire of the tail.
		ring.sq.tail->store(tail_next, std::memory_order_release);
		do_io_uring_enter(ring, submitted);
	}
	return submitted;
}

int io_uring_get_completion(io_uring* ring, io_uring_cqe** cqe_ptr) {

}

} // namespace internal

} // namespace seastar