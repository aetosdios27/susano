#pragma once

#include "susano/fixed_column.hpp"
#include "susano/row_id.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace susano {

inline constexpr std::size_t concurrent_delta_chunk_capacity = 4'096;
inline constexpr std::size_t concurrent_delta_validity_words =
    concurrent_delta_chunk_capacity / 64;

template <FixedColumnValue T>
struct ConcurrentDeltaChunk {
    ConcurrentDeltaChunk() {
        for (auto& word : validity) {
            word.store(0, std::memory_order_relaxed);
        }
    }

    std::array<T, concurrent_delta_chunk_capacity> values{};
    std::array<std::atomic<std::uint64_t>, concurrent_delta_validity_words> validity;
};

template <FixedColumnValue T>
class DeltaReadPrefix {
public:
    DeltaReadPrefix() = default;

    DeltaReadPrefix(std::vector<std::shared_ptr<const ConcurrentDeltaChunk<T>>> chunks,
                    std::size_t size,
                    std::uint64_t logical_base)
        : chunks_{std::move(chunks)}, size_{size}, logical_base_{logical_base} {}

    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }

    [[nodiscard]] std::uint64_t logical_base() const noexcept {
        return logical_base_;
    }

    [[nodiscard]] bool is_null(std::size_t offset) const noexcept {
        assert(offset < size_);
        const auto& chunk = *chunks_[offset / concurrent_delta_chunk_capacity];
        const auto local = offset % concurrent_delta_chunk_capacity;
        const auto word = chunk.validity[local / 64].load(std::memory_order_relaxed);
        return (word & (std::uint64_t{1} << static_cast<unsigned int>(local % 64))) == 0;
    }

    [[nodiscard]] T value(std::size_t offset) const noexcept {
        assert(offset < size_);
        const auto& chunk = *chunks_[offset / concurrent_delta_chunk_capacity];
        return chunk.values[offset % concurrent_delta_chunk_capacity];
    }

    [[nodiscard]] std::size_t storage_bytes() const noexcept {
        return size_ * sizeof(T) + ((size_ + 63) / 64) * sizeof(std::uint64_t);
    }

private:
    std::vector<std::shared_ptr<const ConcurrentDeltaChunk<T>>> chunks_;
    std::size_t size_{0};
    std::uint64_t logical_base_{0};
};

template <FixedColumnValue T>
class ClosedDeltaColumn {
public:
    explicit ClosedDeltaColumn(DeltaReadPrefix<T> prefix) : prefix_{std::move(prefix)} {}

    [[nodiscard]] std::size_t size() const noexcept {
        return prefix_.size();
    }

    [[nodiscard]] std::uint64_t logical_base() const noexcept {
        return prefix_.logical_base();
    }

    [[nodiscard]] bool is_null(std::size_t offset) const noexcept {
        return prefix_.is_null(offset);
    }

    [[nodiscard]] T value(std::size_t offset) const noexcept {
        return prefix_.value(offset);
    }

    [[nodiscard]] const DeltaReadPrefix<T>& prefix() const noexcept {
        return prefix_;
    }

    [[nodiscard]] std::size_t storage_bytes() const noexcept {
        return prefix_.storage_bytes();
    }

private:
    DeltaReadPrefix<T> prefix_;
};

template <FixedColumnValue T>
class ConcurrentDeltaColumn {
public:
    [[nodiscard]] std::size_t visible_size() const noexcept {
        return visible_size_.load(std::memory_order_acquire);
    }

    // Single-writer operation. The release store is the append publication point.
    [[nodiscard]] std::size_t append(T value) {
        return append_impl(value, true);
    }

    [[nodiscard]] std::size_t append_null() {
        return append_impl(T{}, false);
    }

    [[nodiscard]] DeltaReadPrefix<T> snapshot(std::uint64_t logical_base) const {
        const auto size = visible_size_.load(std::memory_order_acquire);
        const auto needed_chunks =
            size / concurrent_delta_chunk_capacity +
            (size % concurrent_delta_chunk_capacity != 0 ? 1 : 0);
        std::vector<std::shared_ptr<const ConcurrentDeltaChunk<T>>> chunks;
        chunks.reserve(needed_chunks);
        {
            std::scoped_lock lock{chunks_mutex_};
            for (std::size_t index = 0; index < needed_chunks; ++index) {
                chunks.push_back(chunks_[index]);
            }
        }
        return DeltaReadPrefix<T>{std::move(chunks), size, logical_base};
    }

    [[nodiscard]] std::shared_ptr<const ClosedDeltaColumn<T>> freeze(
        std::uint64_t logical_base) {
        if (closed_.exchange(true, std::memory_order_acq_rel)) {
            throw std::logic_error{"delta is already closed"};
        }
        return std::make_shared<const ClosedDeltaColumn<T>>(snapshot(logical_base));
    }

    [[nodiscard]] bool closed() const noexcept {
        return closed_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t allocated_bytes() const {
        std::scoped_lock lock{chunks_mutex_};
        return chunks_.size() * sizeof(ConcurrentDeltaChunk<T>);
    }

private:
    [[nodiscard]] std::size_t append_impl(T value, bool valid) {
        if (closed_.load(std::memory_order_acquire)) {
            throw std::logic_error{"cannot append to a closed delta"};
        }

        const auto index = visible_size_.load(std::memory_order_relaxed);
        const auto chunk_index = index / concurrent_delta_chunk_capacity;
        const auto local = index % concurrent_delta_chunk_capacity;
        std::shared_ptr<ConcurrentDeltaChunk<T>> chunk;
        {
            std::scoped_lock lock{chunks_mutex_};
            if (chunk_index == chunks_.size()) {
                chunks_.push_back(std::make_shared<ConcurrentDeltaChunk<T>>());
            }
            chunk = chunks_[chunk_index];
        }

        chunk->values[local] = value;
        const auto bit = std::uint64_t{1} << static_cast<unsigned int>(local % 64);
        auto& validity_word = chunk->validity[local / 64];
        if (valid) {
            validity_word.fetch_or(bit, std::memory_order_relaxed);
        } else {
            validity_word.fetch_and(~bit, std::memory_order_relaxed);
        }
        visible_size_.store(index + 1, std::memory_order_release);
        return index;
    }

    mutable std::mutex chunks_mutex_;
    std::vector<std::shared_ptr<ConcurrentDeltaChunk<T>>> chunks_;
    std::atomic<std::size_t> visible_size_{0};
    std::atomic<bool> closed_{false};
};

}  // namespace susano
