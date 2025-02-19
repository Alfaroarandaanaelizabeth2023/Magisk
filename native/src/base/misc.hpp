#pragma once

#include <pthread.h>
#include <string>
#include <functional>
#include <string_view>
#include <bitset>
#include <random>

#include "xwrap.hpp"

#define DISALLOW_COPY_AND_MOVE(clazz) \
clazz(const clazz &) = delete;        \
clazz(clazz &&) = delete;

#define ALLOW_MOVE_ONLY(clazz) \
clazz() = default;             \
clazz(const clazz&) = delete;  \
clazz(clazz &&o) { swap(o); }  \
clazz& operator=(clazz &&o) { swap(o); return *this; }

class mutex_guard {
    DISALLOW_COPY_AND_MOVE(mutex_guard)
public:
    explicit mutex_guard(pthread_mutex_t &m): mutex(&m) {
        pthread_mutex_lock(mutex);
    }
    void unlock() {
        pthread_mutex_unlock(mutex);
        mutex = nullptr;
    }
    ~mutex_guard() {
        if (mutex) pthread_mutex_unlock(mutex);
    }
private:
    pthread_mutex_t *mutex;
};

template <class Func>
class run_finally {
    DISALLOW_COPY_AND_MOVE(run_finally)
public:
    explicit run_finally(Func &&fn) : fn(std::move(fn)) {}
    ~run_finally() { fn(); }
private:
    Func fn;
};

template <typename T>
class reversed_container {
public:
    reversed_container(T &base) : base(base) {}
    decltype(std::declval<T>().rbegin()) begin() { return base.rbegin(); }
    decltype(std::declval<T>().crbegin()) begin() const { return base.crbegin(); }
    decltype(std::declval<T>().crbegin()) cbegin() const { return base.crbegin(); }
    decltype(std::declval<T>().rend()) end() { return base.rend(); }
    decltype(std::declval<T>().crend()) end() const { return base.crend(); }
    decltype(std::declval<T>().crend()) cend() const { return base.crend(); }
private:
    T &base;
};

template <typename T>
reversed_container<T> reversed(T &base) {
    return reversed_container<T>(base);
}

template<class T>
static inline void default_new(T *&p) { p = new T(); }

template<class T>
static inline void default_new(std::unique_ptr<T> &p) { p.reset(new T()); }

template<typename T, typename Impl>
class stateless_allocator {
public:
    using value_type = T;
    T *allocate(size_t num) { return static_cast<T*>(Impl::allocate(sizeof(T) * num)); }
    void deallocate(T *ptr, size_t num) { Impl::deallocate(ptr, sizeof(T) * num); }
    stateless_allocator()                           = default;
    stateless_allocator(const stateless_allocator&) = default;
    stateless_allocator(stateless_allocator&&)      = default;
    template <typename U>
    stateless_allocator(const stateless_allocator<U, Impl>&) {}
    bool operator==(const stateless_allocator&) { return true; }
    bool operator!=(const stateless_allocator&) { return false; }
};

class dynamic_bitset_impl {
public:
    using slot_type = unsigned long;
    constexpr static int slot_size = sizeof(slot_type) * 8;
    using slot_bits = std::bitset<slot_size>;

    size_t slots() const { return slot_list.size(); }
    slot_type get_slot(size_t slot) const {
        return slot_list.size() > slot ? slot_list[slot].to_ulong() : 0ul;
    }
    void emplace_back(slot_type l) {
        slot_list.emplace_back(l);
    }
protected:
    slot_bits::reference get(size_t pos) {
        size_t slot = pos / slot_size;
        size_t index = pos % slot_size;
        if (slot_list.size() <= slot) {
            slot_list.resize(slot + 1);
        }
        return slot_list[slot][index];
    }
    bool get(size_t pos) const {
        size_t slot = pos / slot_size;
        size_t index = pos % slot_size;
        return slot_list.size() > slot && slot_list[slot][index];
    }
private:
    std::vector<slot_bits> slot_list;
};

struct dynamic_bitset : public dynamic_bitset_impl {
    slot_bits::reference operator[] (size_t pos) { return get(pos); }
    bool operator[] (size_t pos) const { return get(pos); }
};

struct StringCmp {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const { return a < b; }
};

struct heap_data;

// Interchangeable as `&[u8]` in Rust
struct byte_view {
    byte_view() : _buf(nullptr), _sz(0) {}
    byte_view(const void *buf, size_t sz) : _buf((uint8_t *) buf), _sz(sz) {}

    // byte_view, or any of its subclass, can be copied as byte_view
    byte_view(const byte_view &o) : _buf(o._buf), _sz(o._sz) {}

    // Bridging to Rust slice
    byte_view(rust::Slice<const uint8_t> o) : byte_view(o.data(), o.size()) {}
    operator rust::Slice<const uint8_t>() { return rust::Slice<const uint8_t>(_buf, _sz); }

    // String as bytes
    byte_view(const char *s, bool with_nul = true)
    : byte_view(std::string_view(s), with_nul, false) {}
    byte_view(const std::string &s, bool with_nul = true)
    : byte_view(std::string_view(s), with_nul, false) {}
    byte_view(std::string_view s, bool with_nul = true)
    : byte_view(s, with_nul, true /* string_view is not guaranteed to null terminate */ ) {}

    // Vector as bytes
    byte_view(const std::vector<uint8_t> &v) : byte_view(v.data(), v.size()) {}

    const uint8_t *buf() const { return _buf; }
    size_t sz() const { return _sz; }

    bool contains(byte_view pattern) const;
    bool equals(byte_view o) const;
    heap_data clone() const;

protected:
    uint8_t *_buf;
    size_t _sz;

private:
    byte_view(std::string_view s, bool with_nul, bool check_nul)
    : byte_view(static_cast<const void *>(s.data()), s.length()) {
        if (with_nul) {
            if (check_nul && s[s.length()] != '\0')
                return;
            ++_sz;
        }
    }
};

// Interchangeable as `&mut [u8]` in Rust
struct byte_data : public byte_view {
    byte_data() = default;
    byte_data(void *buf, size_t sz) : byte_view(buf, sz) {}

    // We don't want mutable references to be copied or moved around; pass bytes as byte_view
    // Subclasses are free to implement their own constructors
    byte_data(const byte_data &) = delete;
    byte_data(byte_data &&) = delete;

    // Transparent conversion from common C++ types to mutable byte references
    byte_data(std::string &s, bool with_nul = true)
    : byte_data(s.data(), with_nul ? s.length() + 1 : s.length()) {}
    byte_data(std::vector<uint8_t> &v) : byte_data(v.data(), v.size()) {}

    // Bridging to Rust slice
    byte_data(rust::Slice<uint8_t> o) : byte_data(o.data(), o.size()) {}
    operator rust::Slice<uint8_t>() { return rust::Slice<uint8_t>(_buf, _sz); }

    using byte_view::buf;
    using byte_view::sz;
    uint8_t *buf() { return _buf; }
    size_t &sz() { return _sz; }

    void swap(byte_data &o);
    std::vector<size_t> patch(byte_view from, byte_view to);
};

template<size_t N>
struct byte_array : public byte_data {
    byte_array() : byte_data(arr, N), arr{0} {}
private:
    uint8_t arr[N];
};

class byte_channel;

struct heap_data : public byte_data {
    ALLOW_MOVE_ONLY(heap_data)

    explicit heap_data(size_t sz) : byte_data(malloc(sz), sz) {}
    ~heap_data() { free(_buf); }

    // byte_channel needs to reallocate the internal buffer
    friend byte_channel;
};

uint64_t parse_uint64_hex(std::string_view s);
int parse_int(std::string_view s);

using thread_entry = void *(*)(void *);
extern "C" int new_daemon_thread(thread_entry entry, void *arg = nullptr);

static inline bool str_contains(std::string_view s, std::string_view ss) {
    return s.find(ss) != std::string::npos;
}
static inline bool str_starts(std::string_view s, std::string_view ss) {
    return s.size() >= ss.size() && s.compare(0, ss.size(), ss) == 0;
}
static inline bool str_ends(std::string_view s, std::string_view ss) {
    return s.size() >= ss.size() && s.compare(s.size() - ss.size(), std::string::npos, ss) == 0;
}
static inline std::string ltrim(std::string &&s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    return std::move(s);
}
static inline std::string rtrim(std::string &&s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch) && ch != '\0';
    }).base(), s.end());
    return std::move(s);
}

int fork_dont_care();
int fork_no_orphan();
void init_argv0(int argc, char **argv);
void set_nice_name(const char *name);
uint32_t binary_gcd(uint32_t u, uint32_t v);
int switch_mnt_ns(int pid);
std::mt19937_64 &get_rand(const void *seed_buf = nullptr);
int gen_rand_str(char *buf, int len, bool varlen = true);
std::string &replace_all(std::string &str, std::string_view from, std::string_view to);
std::vector<std::string> split(std::string_view s, std::string_view delims);
std::vector<std::string_view> split_view(std::string_view, std::string_view delims);

// Similar to vsnprintf, but the return value is the written number of bytes
int vssprintf(char *dest, size_t size, const char *fmt, va_list ap);
// Similar to snprintf, but the return value is the written number of bytes
int ssprintf(char *dest, size_t size, const char *fmt, ...);
// This is not actually the strscpy from the Linux kernel.
// Silently truncates, and returns the number of bytes written.
extern "C" size_t strscpy(char *dest, const char *src, size_t size);

// Ban usage of unsafe cstring functions
#define vsnprintf  __use_vssprintf_instead__
#define snprintf   __use_ssprintf_instead__
#define strlcpy    __use_strscpy_instead__

struct exec_t {
    bool err = false;
    int fd = -2;
    void (*pre_exec)() = nullptr;
    int (*fork)() = xfork;
    const char **argv = nullptr;
};

int exec_command(exec_t &exec);
template <class ...Args>
int exec_command(exec_t &exec, Args &&...args) {
    const char *argv[] = {args..., nullptr};
    exec.argv = argv;
    return exec_command(exec);
}
int exec_command_sync(exec_t &exec);
template <class ...Args>
int exec_command_sync(exec_t &exec, Args &&...args) {
    const char *argv[] = {args..., nullptr};
    exec.argv = argv;
    return exec_command_sync(exec);
}
template <class ...Args>
int exec_command_sync(Args &&...args) {
    exec_t exec;
    return exec_command_sync(exec, args...);
}
template <class ...Args>
void exec_command_async(Args &&...args) {
    const char *argv[] = {args..., nullptr};
    exec_t exec {
        .fork = fork_dont_care,
        .argv = argv,
    };
    exec_command(exec);
}
