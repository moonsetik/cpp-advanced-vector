#pragma once

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory&) = delete;

    RawMemory(RawMemory&& other) noexcept {
        Swap(other);
    }

    RawMemory& operator=(RawMemory&& other) noexcept {
        if (this != &other) {
            Swap(other);
        }
        return *this;
    }

    T* operator+(size_t offset) noexcept {
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const noexcept {
        return capacity_;
    }

private:
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    Vector() noexcept = default;

    explicit Vector(size_t size)
        : data_(size)
        , size_(size) {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    Vector(const Vector& other)
        : data_(other.size_)
        , size_(other.size_) {
        std::uninitialized_copy_n(other.data_.GetAddress(), other.size_, data_.GetAddress());
    }

    Vector(Vector&& other) noexcept
        : data_(std::move(other.data_))
        , size_(std::exchange(other.size_, 0)) {
    }

    ~Vector() {
        std::destroy_n(data_.GetAddress(), size_);
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                Vector rhs_copy(rhs);
                Swap(rhs_copy);
            }
            else {
                AssignFromNonGrowing(rhs);
            }
        }
        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept {
        if (this != &rhs) {
            Swap(rhs);
        }
        return *this;
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }
        RawMemory<T> new_data(new_capacity);
        ReallocateMove(new_data.GetAddress());
        data_.Swap(new_data);
    }

    void Resize(size_t new_size) {
        if (new_size < size_) {
            std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
        }
        else if (new_size > size_) {
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
        }
        size_ = new_size;
    }

    void PushBack(const T& value) {
        EmplaceBack(value);
    }

    void PushBack(T&& value) {
        EmplaceBack(std::move(value));
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        if (size_ == Capacity()) {
            size_t new_capacity = (size_ == 0 ? 1 : size_ * 2);
            RawMemory<T> new_data(new_capacity);

            T* new_item_ptr = new (new_data + size_) T(std::forward<Args>(args)...);

            try {
                ReallocateMove(new_data.GetAddress());
            }
            catch (...) {
                std::destroy_at(new_item_ptr);
                throw;
            }

            data_.Swap(new_data);
        }
        else {
            new (data_ + size_) T(std::forward<Args>(args)...);
        }
        return data_[size_++];
    }

    void PopBack() noexcept {
        if (size_ > 0) {
            std::destroy_at(data_ + (size_ - 1));
            --size_;
        }
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    iterator begin() noexcept {
        return data_.GetAddress();
    }

    iterator end() noexcept {
        return data_.GetAddress() + size_;
    }

    const_iterator begin() const noexcept {
        return data_.GetAddress();
    }

    const_iterator end() const noexcept {
        return data_.GetAddress() + size_;
    }

    const_iterator cbegin() const noexcept {
        return begin();
    }

    const_iterator cend() const noexcept {
        return end();
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        iterator it = const_cast<iterator>(pos);
        assert(it >= begin() && it <= end());

        size_t index = it - begin();

        if (index == size_) {
            return &EmplaceBack(std::forward<Args>(args)...);
        }

        if (size_ == Capacity()) {
            size_t new_capacity = (size_ == 0 ? 1 : size_ * 2);
            RawMemory<T> new_data(new_capacity);
            size_t new_size = 0;

            try {
                std::uninitialized_move_n(data_.GetAddress(), index, new_data.GetAddress());
                new_size += index;

                new (new_data + index) T(std::forward<Args>(args)...);
                ++new_size;

                std::uninitialized_move_n(data_.GetAddress() + index, size_ - index,
                    new_data.GetAddress() + index + 1);
                new_size += (size_ - index);
            }
            catch (...) {
                std::destroy_n(new_data.GetAddress(), new_size);
                throw;
            }

            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
            size_ = new_size;
            return data_.GetAddress() + index;
        }

        T* const address = data_.GetAddress();

        bool use_temp = false;
        alignas(T) unsigned char temp_buffer[sizeof(T)];

        if constexpr (sizeof...(Args) == 1) {
            auto check_and_copy_arg = [&](auto&& arg) {
                using Arg = decltype(arg);
                if constexpr (std::is_lvalue_reference_v<Arg>) {
                    using ValueType = std::remove_cvref_t<Arg>;
                    if constexpr (std::is_same_v<ValueType, T>) {
                        const void* arg_addr = static_cast<const void*>(&arg);
                        const void* buf_start = static_cast<const void*>(address);
                        const void* buf_end = static_cast<const void*>(address + size_);

                        if (arg_addr >= buf_start && arg_addr < buf_end) {
                            if constexpr (std::is_copy_constructible_v<T>) {
                                new (temp_buffer) T(arg);
                                use_temp = true;
                            }
                        }
                    }
                }
                };
            check_and_copy_arg(std::get<0>(std::forward_as_tuple(std::forward<Args>(args)...)));
        }

        new (address + size_) T(std::move_if_noexcept(address[size_ - 1]));
        ++size_;

        try {
            std::move_backward(address + index, address + size_ - 1, address + size_);
            std::destroy_at(address + index);

            if (use_temp) {
                new (address + index) T(std::move(*reinterpret_cast<T*>(temp_buffer)));
            }
            else {
                new (address + index) T(std::forward<Args>(args)...);
            }
        }
        catch (...) {
            std::destroy_at(address + size_ - 1);
            --size_;
            if (use_temp) {
                std::destroy_at(reinterpret_cast<T*>(temp_buffer));
            }
            throw;
        }

        if (use_temp) {
            std::destroy_at(reinterpret_cast<T*>(temp_buffer));
        }

        return address + index;
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    iterator Erase(const_iterator pos) {
        iterator it = const_cast<iterator>(pos);
        assert(it >= begin() && it < end());

        std::move(it + 1, end(), it);
        std::destroy_at(data_ + size_ - 1);
        --size_;
        return it;
    }

private:
    void AssignFromNonGrowing(const Vector& rhs) {
        const size_t copy_count = std::min(size_, rhs.size_);
        std::copy_n(rhs.data_.GetAddress(), copy_count, data_.GetAddress());

        if (rhs.size_ < size_) {
            std::destroy_n(data_.GetAddress() + rhs.size_, size_ - rhs.size_);
        }
        else if (rhs.size_ > size_) {
            std::uninitialized_copy_n(rhs.data_.GetAddress() + size_, rhs.size_ - size_,
                data_.GetAddress() + size_);
        }
        size_ = rhs.size_;
    }

    void ReallocateMove(T* new_buf) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_buf);
        }
        else {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_buf);
        }
        std::destroy_n(data_.GetAddress(), size_);
    }

    RawMemory<T> data_;
    size_t size_ = 0;
};