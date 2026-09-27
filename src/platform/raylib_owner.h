#pragma once
#include <utility>

// Move-only C-resource owners. All borrowed handles expire when the owner resets.
template<class T, bool (*Valid)(T), void (*Release)(T)>
class Owned final {
    T value_{};
public:
    Owned() noexcept=default;
    explicit Owned(T value) noexcept:value_(value) {}
    Owned(const Owned&)=delete;
    Owned& operator=(const Owned&)=delete;
    Owned(Owned&& other) noexcept:value_(std::exchange(other.value_,T{})) {}
    Owned& operator=(Owned&& other) noexcept {
        if (this!=&other) reset(std::exchange(other.value_,T{}));
        return *this;
    }
    ~Owned() { reset(); }
    void reset(T value={}) noexcept { if (Valid(value_)) Release(value_); value_=value; }
    [[nodiscard]] T get() const noexcept { return value_; }
    [[nodiscard]] T* ptr() noexcept { return &value_; }
    explicit operator bool() const noexcept { return Valid(value_); }
};
