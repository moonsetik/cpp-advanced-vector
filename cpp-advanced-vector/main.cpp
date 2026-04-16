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
            Deallocate(buffer_);
            buffer_ = nullptr;
            capacity_ = 0;
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
            } else {
                if (rhs.size_ < size_) {
                    std::copy(rhs.data_.GetAddress(), rhs.data_.GetAddress() + rhs.size_, data_.GetAddress());
                    std::destroy_n(data_.GetAddress() + rhs.size_, size_ - rhs.size_);
                } else {
                    std::copy(rhs.data_.GetAddress(), rhs.data_.GetAddress() + size_, data_.GetAddress());
                    std::uninitialized_copy_n(rhs.data_.GetAddress() + size_, rhs.size_ - size_, data_.GetAddress() + size_);
                }
                size_ = rhs.size_;
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
        } else if (new_size > size_) {
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
            } catch (...) {
                std::destroy_at(new_item_ptr);
                throw;
            }

            data_.Swap(new_data);
        } else {
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

    // Итераторы
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

    // Вставка элемента с perfect forwarding
    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        iterator it = const_cast<iterator>(pos);
        size_t index = it - begin();

        if (size_ == Capacity()) {
            // Реаллокация
            size_t new_capacity = (size_ == 0 ? 1 : size_ * 2);
            RawMemory<T> new_data(new_capacity);
            size_t new_size = 0;

            try {
                if (index == size_ && (std::is_copy_constructible_v<T> || std::is_nothrow_move_constructible_v<T>)) {
                    // Строгая гарантия для вставки в конец
                    new (new_data + index) T(std::forward<Args>(args)...);
                    for (size_t i = 0; i < size_; ++i) {
                        new (new_data + i) T(std::move_if_noexcept(data_[i]));
                        ++new_size;
                    }
                    new_size = size_ + 1;
                } else {
                    // Базовая гарантия: создаём последовательно
                    for (size_t i = 0; i < index; ++i) {
                        new (new_data + i) T(std::move_if_noexcept(data_[i]));
                        ++new_size;
                    }
                    new (new_data + index) T(std::forward<Args>(args)...);
                    ++new_size;
                    for (size_t i = index; i < size_; ++i) {
                        new (new_data + i + 1) T(std::move_if_noexcept(data_[i]));
                        ++new_size;
                    }
                }
            } catch (...) {
                std::destroy_n(new_data.GetAddress(), new_size);
                throw;
            }

            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
            size_ = new_size;
            return data_.GetAddress() + index;
        } else {
            // Без реаллокации
            if (index == size_) {
                new (data_ + size_) T(std::forward<Args>(args)...);
                ++size_;
                return data_ + index;
            } else {
                size_t old_size = size_;
                try {
                    new (data_ + size_) T(std::move_if_noexcept(data_[size_ - 1]));
                } catch (...) {
                    throw;
                }
                ++size_;

                try {
                    for (size_t i = old_size - 1; i > index; --i) {
                        data_[i] = std::move_if_noexcept(data_[i - 1]);
                    }
                    std::destroy_at(data_ + index);
                    new (data_ + index) T(std::forward<Args>(args)...);
                } catch (...) {
                    // Откат до согласованного состояния (базовая гарантия)
                    std::destroy_n(data_ + index, size_ - index);
                    size_ = index;
                    throw;
                }
                return data_ + index;
            }
        }
    }

    // Вставка копированием
    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    // Вставка перемещением
    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    // Удаление элемента по итератору
    iterator Erase(const_iterator pos) /*noexcept(std::is_nothrow_move_assignable_v<T>)*/ {
        iterator it = const_cast<iterator>(pos);
        size_t index = it - begin();
        assert(index < size_);

        for (size_t i = index; i + 1 < size_; ++i) {
            data_[i] = std::move_if_noexcept(data_[i + 1]);
        }
        std::destroy_at(data_ + size_ - 1);
        --size_;
        return it;
    }

private:
    // Перенос существующих элементов при реаллокации
    void ReallocateMove(T* new_buf) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_buf);
        } else {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_buf);
        }
        std::destroy_n(data_.GetAddress(), size_);
    }

    RawMemory<T> data_;
    size_t size_ = 0;
};