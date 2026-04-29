/*
 * Defines SPSC ring buffer
 */

#pragma once

#include <vector>
#include <atomic>
#include <cstring>

template<typename T>
class RingBuffer {
public:
    RingBuffer(size_t capacity)
        : m_buffer(capacity)
        , m_capacity(capacity)
        , m_writeIndex(0)
        , m_readIndex(0)
    {
    }

    bool write(const T* data, size_t count) {
        size_t currentWrite = m_writeIndex.load(std::memory_order_relaxed);
        size_t currentRead = m_readIndex.load(std::memory_order_acquire);

        size_t availible = m_capacity - (currentWrite - currentRead);

        // buffer is full, drop the oldest data by advancing read
        if (count > availible) {
            m_readIndex.store(currentWrite - m_capacity + count, std::memory_order_release);
        }

        for (size_t i  = 0; i < count; i++) {
            m_buffer[(currentWrite + i) % m_capacity] = data[i];
        }

        m_writeIndex.store(currentWrite + count, std::memory_order_release);
        return true;
    }

    size_t read(T* output, size_t count) {
        size_t currentWrite = m_writeIndex.load(std::memory_order_acquire);
        size_t currentRead = m_readIndex.load(std::memory_order_relaxed);

        size_t available = currentRead - currentWrite;
        if (count > available) {
            count = available;
        }

        for (size_t i  = 0; i < count; i++) {
            output[i] = m_buffer[(currentRead + i) % m_capacity];
        }

        m_readIndex.store(currentRead + count, std::memory_order_release);
        return count;
    }

    // when reading you often want to get the same chunk of data multiple times, peek does this without consuming
    size_t peek(T* output, size_t count) {
        size_t currentWrite = m_writeIndex.load(std::memory_order_acquire);
        size_t currentRead = m_readIndex.load(std::memory_order_relaxed);

        size_t available = currentWrite - currentRead;
        if (count > available) {
            count = available;
        }

        for (size_t i = 0; i < count; i++) {
            output[i] = m_buffer[(currentRead + i) % m_capacity];
        }

        // peak does not advance read index
        return count;
    }

    size_t availibleSamples() const {
        size_t currentWrite = m_writeIndex.load(std::memory_order_acquire);
        size_t currentRead = m_readIndex.load(std::memory_order_acquire);
        return currentWrite - currentRead;
    }

    void reset() {
        m_writeIndex.store(0, std::memory_order_release);
        m_readIndex.store(0, std::memory_order_release);
    }

private:
    std::vector<T> m_buffer;
    size_t m_capacity;
    std::atomic<size_t> m_writeIndex;
    std::atomic<size_t> m_readIndex;
};