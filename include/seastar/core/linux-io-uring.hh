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

#pragma once

#include <seastar/util/defer.hh>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstddef>
#include <signal.h>
#include <system_error>
#include <unistd.h>

namespace seastar {

namespace internal {

namespace linux_abi {

template<typename T>
inline T operator|(T a, T b) {
    return T(std::underlying_type_t<T>(a) | std::underlying_type_t<T>(b));
}

template<typename T>
inline void operator|=(T& a, T b) {
    a = (a | b);
}

template<typename T>
inline T operator&(T a, T b) {
    return T(std::underlying_type_t<T>(a) & std::underlying_type_t<T>(b));
}

template<typename T>
inline void operator&=(T& a, T b) {
    a = (a & b);
}

using io_uring_context_t = uint64_t;

enum class io_uring_opcode : uint8_t {
    NOP = 0,
    READV = 1,
    WRITEV = 2,
    FSYNC = 3,
    READ_FIXED = 4,
    WRITE_FIXED = 5,
    POLL_ADD = 6,
    POLL_REMOVE = 7,
};

enum class io_uring_flags : uint8_t {
    FIXED_FILE	= 1 << 0,
};

enum class io_uring_sqe_fsync_flags : uint32_t {
    DATASYNC = 1 << 0,
};

// IO submission data structure (Submission Queue Entry)
struct io_uring_sqe {
    io_uring_opcode opcode;
    io_uring_flags flags;
    uint16_t ioprio;
    int32_t fd;
    uint64_t offset;
    // Pointer to an io_vec array if the operation is a vectored read/write.
    // Otherwise, pointer to a buffer.
    uint64_t address;
    // Buffer size for a non-vectored IO transfer, or a number of iovecs.
    uint32_t length;
    union { // Flags specific to the opcode.
        int32_t rw_flags;
        io_uring_sqe_fsync_flags fsync_flags;
        uint16_t poll_events;
    };
    io_uring_context_t user_data;
    union {
        uint16_t buf_index;
        uint64_t __pad2[3];
    };
};

// IO completion data structure (Completion Queue Entry)
struct io_uring_cqe {
    io_uring_context_t user_data;
    int32_t result_code;
    uint32_t flags; // Unused.
};

// Magic offsets for mmap
constexpr uint64_t IORING_OFF_SQ_RING = 0;
constexpr uint64_t IORING_OFF_CQ_RING = 0x8000000;
constexpr uint64_t IORING_OFF_SQES = 0x10000000;

// The following structs are filled with the offsets for mmap(2).
struct io_uring_offsets_base {
	uint32_t head;
	uint32_t tail;
	uint32_t ring_mask;
	uint32_t ring_entries;
};

struct io_sqring_offsets : public io_uring_offsets_base {
	uint32_t flags;
	uint32_t dropped;
	uint32_t index_array;
	uint32_t resv1;
	uint64_t resv2;
};

struct io_cqring_offsets : public io_uring_offsets_base {
	uint32_t overflow;
	uint32_t cqes;
	uint64_t resv[2];
};

enum class io_uring_setup_flags : uint32_t {
    IOPOLL = 1 << 0, // Polling instead of interrupts
    SQPOLL = 1 << 1, // Kernel-side polling
    SQ_AFF = 1 << 2, // sq_thread_cpu sets CPU affinity for kernel poller
};

// Configuration values for io_uring_setup.
// Copied back with updated info on success.
struct io_uring_params {
	uint32_t sq_entries;
	uint32_t cq_entries;
	io_uring_setup_flags flags;
	uint32_t sq_thread_cpu;
	uint32_t sq_thread_idle;
	uint32_t resv[5];
	io_sqring_offsets sq_off;
	io_cqring_offsets cq_off;
};

int io_uring_setup(uint32_t entries, io_uring_params*);

enum class io_uring_enter_flags : uint8_t {
    GETEVENTS = 1 << 0,
    SQ_WAKEUP = 1 << 1, // Wakes up the kernel-side poller thread.
};

int io_uring_enter(unsigned fd, unsigned to_submit, unsigned min_complete, io_uring_enter_flags, sigset_t*);

enum class io_uring_register_opcode : uint32_t {
    IORING_REGISTER_BUFFERS = 0,
    IORING_UNREGISTER_BUFFERS = 1,
    IORING_REGISTER_FILES = 2,
    IORING_UNREGISTER_FILES = 3,
};

// Every time a file descriptor is filled into an sqe and submitted, the kernel
// must retrieve a reference to it. Once I/O has completed, the file reference
// is dropped again. Due to the atomic nature of this file reference, this can
// be a noticeable slowdown for high IOPS workloads. To alleviate this issue,
// io_uring offers this system call as a way to pre-register a file-set for an
// io_uring instance.
int io_uring_register(int fd, io_uring_register_opcode, void* arg, unsigned nr_args);

} // namespace linux_abi

struct io_uring_base {
	// Head and tail offsets into the ring; the offsets need to be
	// masked to get valid indices.
    std::atomic<uint32_t>* head;
    std::atomic<uint32_t>* tail;
	// The ring size, a power of 2 constant.
    uint32_t ring_size;
	// Bitmask to apply to head and tail.
	uint32_t ring_mask;
};

enum class io_uring_sq_flags : uint8_t {
    NEED_WAKEUP = 1 << 0, // Needs linux_abi::io_uring_enter_flags::SQ_WAKEUP.
};

// Submission ring. Kernel controls head and seastar controls tail.
struct io_uring_sq : public io_uring_base {
	// Runtime flags written by the kernel, shouldn't be modified by
	// seastar. A full memory barrier is needed before checking
	// for io_uring_sq_flags::NEED_WAKEUP after updating the tail.
    uint32_t* flags;
    // Number of invalid entries dropped by the kernel due to
	// invalid index stored in array. Written by the kernel.
	uint32_t* dropped;
	// Ring buffer of indices into `sqes`. This indirection could
	// be used to assign fixed io_uring_sqe entries to operations
	// and only submit them to the queue when needed.
	uint32_t* index_array;
 	// mmapped seastar using the IORING_OFF_SQES offset.
	linux_abi::io_uring_sqe* sqes;
	// Offsets for the `sqes` array, used by the application.
	uint32_t sqe_head;
	uint32_t sqe_tail;
	// Whether there are pending entries in `sqes`.
	bool pending;
};

// Completion ring. Kernel controls tail and seastar controls head.
struct io_uring_cq : public io_uring_base {
	// Number of completion events lost because the queue was full;
	// written by the kernel.
    uint32_t* overflow;
	// Ring buffer of completion events.
    linux_abi::io_uring_cqe* cqes;
};

struct io_uring {
	io_uring_sq sq;
	io_uring_cq cq;
	int ring_fd;
};

/*
 * Initializes the io_uring submission and completion queues.
 * Support for kernel-side polling is not provided.
 */
void io_uring_queue_init(uint32_t entries, io_uring&, linux_abi::io_uring_setup_flags);

/*
 * Unmaps the memory used by the ring and closes its file descriptor.
 */
void io_uring_queue_exit(io_uring& ring);

/*
 * Return an IO completion, if one is readily available.
 *
 * Returns
 */
linux_abi::io_uring_cqe io_uring_get_completion(io_uring& ring);

unsigned io_uring_get_completion(io_uring& ring, std::vector<linux_abi::io_uring_cqe>&);

int io_uring_wait_completion(io_uring& ring, io_uring_cqe** cqe_ptr);

/*
 * Submits SQE's acquired from io_uring_get_sqe() to the kernel.
 *
 * Returns the number of submitted SQE's.
 */
unsigned io_uring_submit(io_uring& ring);

/*
 * Returns an SQE to fill. io_uring_submit() must later be called
 * to submit all of the requested SQE's.
 *
 * Returns a vacant SQE, or nullptr if none is available.
 */
inline
linux_abi::io_uring_sqe*
io_uring_get_sqe(io_uring& ring) {
	auto next = ring.sq.sqe_tail + 1;
	if (next - ring.sq.sqe_head > ring.sq.ring_size) {
		return nullptr;
	}
	auto* sqe = &ring.sq.sqes[ring.sq.sqe_tail & ring.sq.ring_mask];
    ring.sq.sqe_tail = next;
	return sqe;
}


} // namespace internal

} // namespace seastar