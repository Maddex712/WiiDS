#pragma once

#include <Dolphin/math.h>
#include <Dolphin/types.h>

#include <SMS/assert.h>

#include <JSystem/initializer_list.hxx>
#include <JSystem/memory.hxx>
#include <JSystem/utility.hxx>

#include <JSystem/JGadget/Allocator.hxx>
#include <JSystem/JGadget/Node.hxx>
#include <JSystem/bits/c++config.h>

namespace JGadget {
    template <class _T, class _Alloc = TAllocator<_T>> class TVector {
    public:
        typedef _T value_type;
        typedef _Alloc allocator_type;
        typedef size_t size_type;
        typedef ptrdiff_t difference_type;
        typedef value_type &reference;
        typedef const value_type &const_reference;
        typedef typename _Alloc::pointer pointer;
        typedef typename _Alloc::const_pointer const_pointer;

        struct const_iterator;

        struct iterator {
            typedef _T value_type;
            typedef ptrdiff_t difference_type;
            typedef typename _Alloc::pointer pointer;
            typedef typename _Alloc::const_pointer const_pointer;

            friend class TVector;
            friend struct TVector::const_iterator;

            _GLIBCXX20_CONSTEXPR iterator(const iterator &iter) = default;

        private:
            explicit _GLIBCXX20_CONSTEXPR iterator(pointer node) : mCurrent(node) {}
            // For internal conversion (erase)
            _GLIBCXX20_CONSTEXPR iterator(const const_iterator &iter)
                : mCurrent(const_cast<pointer>(iter.mCurrent)) {}

        public:
            _GLIBCXX20_CONSTEXPR bool operator==(const iterator &rhs) const {
                return mCurrent == rhs.mCurrent;
            }
            _GLIBCXX20_CONSTEXPR bool operator!=(const iterator &rhs) const {
                return mCurrent != rhs.mCurrent;
            }

            _GLIBCXX20_CONSTEXPR iterator operator+(int i) {
                iterator temp{mCurrent};
                temp->mCurrent += i;
                return temp;
            }

            _GLIBCXX20_CONSTEXPR iterator &operator+=(int i) {
                mCurrent += i;
                return *this;
            }

            _GLIBCXX20_CONSTEXPR iterator &operator++() {
                ++mCurrent;
                return *this;
            }

            _GLIBCXX20_CONSTEXPR iterator operator++(int) {
                iterator temp{mCurrent};
                ++mCurrent;
                return temp;
            }

            _GLIBCXX20_CONSTEXPR iterator operator-(int i) {
                iterator temp{mCurrent};
                temp->mCurrent -= i;
                return temp;
            }

            _GLIBCXX20_CONSTEXPR iterator &operator-=(int i) {
                mCurrent -= i;
                return *this;
            }

            _GLIBCXX20_CONSTEXPR iterator &operator--() {
                --mCurrent;
                return *this;
            }

            _GLIBCXX20_CONSTEXPR iterator operator--(int) {
                iterator temp{mCurrent};
                --mCurrent;
                return temp;
            }

            pointer operator->() const { return mCurrent; }
            reference operator*() const { return *mCurrent; }

        private:
            pointer mCurrent;
        };

        struct const_iterator {
            typedef _T value_type;
            typedef ptrdiff_t difference_type;
            typedef typename _Alloc::pointer pointer;
            typedef typename _Alloc::const_pointer const_pointer;

            friend class TVector;
            friend struct TVector::iterator;

            _GLIBCXX20_CONSTEXPR const_iterator(const iterator &iter) : mCurrent(iter.mCurrent) {}
            _GLIBCXX20_CONSTEXPR const_iterator(const const_iterator &iter) = default;

        private:
            explicit _GLIBCXX20_CONSTEXPR const_iterator(const_pointer node) : mCurrent(node) {}

        public:
            _GLIBCXX20_CONSTEXPR bool operator==(const const_iterator &rhs) const {
                return mCurrent == rhs.mCurrent;
            }
            _GLIBCXX20_CONSTEXPR bool operator!=(const const_iterator &rhs) const {
                return mCurrent != rhs.mCurrent;
            }

            _GLIBCXX20_CONSTEXPR const_iterator operator+(int i) {
                return const_iterator(mCurrent + i);
            }

            _GLIBCXX20_CONSTEXPR const_iterator &operator+=(int i) {
                mCurrent += i;
                return *this;
            }

            _GLIBCXX20_CONSTEXPR const_iterator &operator++() {
                ++mCurrent;
                return *this;
            }

            _GLIBCXX20_CONSTEXPR const_iterator operator++(int) {
                const_iterator temp = const_iterator(mCurrent);
                ++mCurrent;
                return temp;
            }

            _GLIBCXX20_CONSTEXPR const_iterator operator-(int i) {
                return const_iterator(mCurrent - i);
            }

            _GLIBCXX20_CONSTEXPR const_iterator &operator-=(int i) {
                mCurrent -= i;
                return *this;
            }

            _GLIBCXX20_CONSTEXPR const_iterator &operator--() {
                --mCurrent;
                return *this;
            }

            _GLIBCXX20_CONSTEXPR const_iterator operator--(int) {
                const_iterator temp = const_iterator(mCurrent);
                --mCurrent;
                return temp;
            }

            const_pointer operator->() const { return mCurrent; }
            const_reference operator*() const { return *mCurrent; }

        private:
            const_pointer mCurrent;
        };

        _GLIBCXX20_CONSTEXPR TVector() _GLIBCXX_NOEXCEPT_IF(_GLIBCXX_NOEXCEPT_IF(allocator_type()))
            : mAllocator(), mBegin(nullptr), mEnd(nullptr), mCapacity(0), mGrowthFactor(2.0f),
              mNextResize(0) {}

        explicit _GLIBCXX20_CONSTEXPR TVector(const allocator_type &allocator) _GLIBCXX_NOEXCEPT
            : TVector() {
            mAllocator = allocator;
        }

#if __cplusplus >= 201103L
        _GLIBCXX20_CONSTEXPR TVector(size_type count, const value_type &value,
                                     const allocator_type &allocator = allocator_type())
            : TVector(allocator) {
            insert(end(), count, value);
        }
#else
        explicit TVector(size_type count, const value_type &value) : TVector(allocator_type()) {
            insert(end(), count, value);
        }
#endif

#if __cplusplus >= 201402L
        explicit _GLIBCXX20_CONSTEXPR TVector(size_type count,
                                              const allocator_type &allocator = allocator_type())
            : TVector(allocator) {
            insert(end(), count, value_type());
        }
#else
        explicit TVector(size_type count) : TVector(allocator_type()) {
            insert(end(), count, value_type());
        }
#endif

        _GLIBCXX20_CONSTEXPR TVector(const TVector &other) : TVector(other.mAllocator) {
            mGrowthFactor = other.mGrowthFactor;
            mNextResize   = other.mNextResize;
            for (auto &i : other) {
                insert(end(), i);
            }
        }

#if __cplusplus >= 201103L
        _GLIBCXX20_CONSTEXPR TVector(const TVector &other, const allocator_type &allocator)
            : TVector(allocator) {
            mGrowthFactor = other.mGrowthFactor;
            mNextResize   = other.mNextResize;
            for (auto &i : other) {
                insert(end(), i);
            }
        }

        _GLIBCXX20_CONSTEXPR TVector(TVector &&other) : TVector(other.mAllocator) {
            mGrowthFactor = other.mGrowthFactor;
            mNextResize   = other.mNextResize;
            for (auto &i : other) {
                insert(end(), i);
            }
        }

        _GLIBCXX20_CONSTEXPR TVector(TVector &&other, const allocator_type &allocator)
            : TVector(allocator) {
            mGrowthFactor = other.mGrowthFactor;
            mNextResize   = other.mNextResize;
            for (auto &i : other) {
                insert(end(), i);
            }
        }

        _GLIBCXX20_CONSTEXPR TVector(JSystem::initializer_list<value_type> list,
                                     const allocator_type &allocator = allocator_type())
            : TVector(allocator) {
            for (auto &i : list) {
                insert(end(), i);
            }
        }
#endif

        _GLIBCXX20_CONSTEXPR ~TVector() {
            clear();
            mAllocator.deallocate(mBegin, 1);
        }

        _GLIBCXX20_CONSTEXPR TVector &operator=(const TVector &other) {
            clear();
            for (const auto &i : other) {
                insert(end(), i);
            }
            return *this;
        }

#if __cplusplus >= 201103L
        _GLIBCXX20_CONSTEXPR TVector &operator=(TVector &&other) {
            clear();
            for (const auto &i : other) {
                insert(end(), i);
            }
            return *this;
        }

        _GLIBCXX20_CONSTEXPR TVector &operator=(JSystem::initializer_list<value_type> list) {
            clear();
            for (const auto &i : list) {
                insert(end(), i);
            }
            return *this;
        }
#endif

        _GLIBCXX20_CONSTEXPR reference operator[](size_type index) { return mBegin[index]; }
        _GLIBCXX20_CONSTEXPR const_reference operator[](size_type index) const {
            return mBegin[index];
        }

        _GLIBCXX20_CONSTEXPR allocator_type get_allocator() const { return mAllocator; }

        _GLIBCXX20_CONSTEXPR reference at(size_type index) {
            if (index >= size()) {
                OSPanic(__FILE__, __LINE__,
                        "Attempted OOB access to Vector of size %lu (index %lu)!", size(), index);
                __OSUnhandledException(6, OSGetCurrentContext(), 0);
            }
            return mBegin[index];
        }

        _GLIBCXX20_CONSTEXPR const_reference at(size_type index) const {
            if (index >= size()) {
                OSPanic(__FILE__, __LINE__,
                        "Attempted OOB access to Vector of size %lu (index %lu)!", size(), index);
                __OSUnhandledException(6, OSGetCurrentContext(), 0);
            }
            return mBegin[index];
        }

        _GLIBCXX20_CONSTEXPR reference front() { return *mBegin; }
        _GLIBCXX20_CONSTEXPR const_reference front() const { return *mBegin; }

        _GLIBCXX20_CONSTEXPR reference back() { return *(mEnd - 1); }
        _GLIBCXX20_CONSTEXPR const_reference back() const { return *(mEnd - 1); }

        _GLIBCXX20_CONSTEXPR void assign(size_type count, const_reference value) {
            clear();
            insert(begin(), count, value);
        }

#if __cplusplus >= 201103L
        _GLIBCXX20_CONSTEXPR void assign(JSystem::initializer_list<value_type> list) {
            clear();
            for (auto &i : list) {
                insert(end(), i);
            }
        }
#endif

        _GLIBCXX_NODISCARD _GLIBCXX20_CONSTEXPR bool empty() const _GLIBCXX_NOEXCEPT {
            return mBegin == mEnd;
        }
        _GLIBCXX20_CONSTEXPR void clear() _GLIBCXX_NOEXCEPT { erase(&(*begin()), &(*end())); }

        _GLIBCXX20_CONSTEXPR size_type capacity() const _GLIBCXX_NOEXCEPT { return mCapacity; }
        _GLIBCXX20_CONSTEXPR size_type max_size() const _GLIBCXX_NOEXCEPT {
            return difference_type(-1) / sizeof(difference_type);
        }
        _GLIBCXX20_CONSTEXPR size_type size() const _GLIBCXX_NOEXCEPT {
            return mBegin ? mEnd - mBegin : 0;
        }

        _GLIBCXX20_CONSTEXPR iterator begin() _GLIBCXX_NOEXCEPT { return iterator(mBegin); }
        _GLIBCXX20_CONSTEXPR const_iterator begin() const _GLIBCXX_NOEXCEPT {
            return const_iterator(mBegin);
        }
        _GLIBCXX20_CONSTEXPR iterator end() _GLIBCXX_NOEXCEPT { return iterator(mEnd); }
        _GLIBCXX20_CONSTEXPR const_iterator end() const _GLIBCXX_NOEXCEPT {
            return const_iterator(mEnd);
        }

#if __cplusplus >= 201103L
        _GLIBCXX20_CONSTEXPR const_iterator cbegin() const _GLIBCXX_NOEXCEPT {
            return const_iterator(mBegin);
        }
        _GLIBCXX20_CONSTEXPR const_iterator cend() const _GLIBCXX_NOEXCEPT {
            return const_iterator(mEnd);
        }
#endif

#if __cplusplus >= 201103L
        _GLIBCXX20_CONSTEXPR iterator erase(const_iterator a) {
            return erase(&(*iterator(a)), &(*iterator(a + 1)));
        }
        _GLIBCXX20_CONSTEXPR iterator erase(const_iterator a, const_iterator b) {
            return erase(&(*iterator(a)), &(*iterator(b)));
        }
#else
        iterator erase(iterator a) { return erase(&(*a), &(*(a + 1))); }
        iterator erase(iterator a, iterator b) { return erase(&(*a), &(*b)); }
#endif

    private:
        _GLIBCXX20_CONSTEXPR iterator erase(pointer a, pointer b) {
            DestroyElement_(a, b);
            mEnd = JSystem::copy(b, mEnd, a);
            return iterator(a);
        }

    public:
        _GLIBCXX20_CONSTEXPR iterator insert(const_iterator at, value_type &&item) {
            return insert(&(*iterator(at)), 1, item);
        }
        _GLIBCXX20_CONSTEXPR iterator insert(const_iterator at, const_reference item) {
            return insert(&(*iterator(at)), 1, item);
        }
        _GLIBCXX20_CONSTEXPR iterator insert(const_iterator at, size_type count,
                                             const_reference item) {
            return insert(&(*iterator(at)), count, item);
        }
        _GLIBCXX20_CONSTEXPR iterator insert(const_iterator at, size_type count,
                                             value_type &&item) {
            value_type tmp = item;
            return insert(&(*iterator(at)), count, tmp);
        }

    private:
        _GLIBCXX20_CONSTEXPR iterator insert(pointer at, size_type count, const_reference item) {
            auto ofs   = at - mBegin;
            auto *data = InsertRaw(at, count);
            if (data != &(*end())) {
                JSystem::uninitialized_fill_n(data, count, item);
            }
            return iterator(mBegin + ofs);
        }

    public:
#if __cplusplus >= 201103L
        _GLIBCXX20_CONSTEXPR iterator insert(const_iterator at,
                                             JSystem::initializer_list<value_type> list) {
            for (auto &i : list) {
                at = insert(at, i);
            }
        }

        template <class... _Args>
        _GLIBCXX20_CONSTEXPR iterator emplace(const_iterator at, _Args &&...args) {
            auto *ofs  = at - mBegin;
            auto *data = InsertRaw(at, 1);
            if (data != end()) {
#if __cplusplus <= 201703L
                mAllocator.construct(data, JSystem::forward<_Args>(args)...);
#else
                mAllocator.construct_at(data, JSystem::forward<_Args>(args)...);
#endif
            }
            return mBegin + ofs;
        }
#endif

        _GLIBCXX20_CONSTEXPR void push_back(const_reference item) { insert(end(), item); }
        _GLIBCXX20_CONSTEXPR void pop_back() { erase(end()); }

#if __cplusplus >= 201103L
        template <class... _Args> _GLIBCXX20_CONSTEXPR void emplace_back(_Args &&...args) {
            emplace(end(), JSystem::forward<_Args>(args)...);
        }
#endif

        _GLIBCXX20_CONSTEXPR void reserve(size_type capacity) {
            if (mCapacity >= capacity)
                return;

            pointer reservedBuf = mAllocator.allocate(capacity);
            if (!reservedBuf)
                return;

            pointer b      = mBegin;
            pointer e      = mEnd;
            size_type size = e - b;

            auto resBufIter = reservedBuf;
            for (pointer i = b; i != e; ++i) {
                *resBufIter++ = *i;
            }

            DestroyElement_(b, e);

            mEnd      = reservedBuf + size;
            mBegin    = reservedBuf;
            mCapacity = capacity;

            mAllocator.deallocate(b, 1);
        }

        _GLIBCXX20_CONSTEXPR void resize(size_type s) { resize(s, value_type()); }

#if __cplusplus >= 201103L
        _GLIBCXX20_CONSTEXPR void resize(size_type s, const value_type &value) {
            const size_type currentSize = size();
            if (s == currentSize)
                return;

            if (s < currentSize) {
                erase(begin() + s, end());
            } else {
                insert(end(), s - currentSize, value);
            }
        }
#else
        void resize(size_type s, value_type value) {
            const size_type currentSize = size();
            if (s == currentSize)
                return;

            if (s < currentSize) {
                erase(begin() + s, end());
            } else {
                insert(end(), s - currentSize, value);
            }
        }
#endif

    private:
        _GLIBCXX20_CONSTEXPR void DestroyElement_(pointer a, pointer b) {
            for (; a != b; ++a) {
                mAllocator.destroy(a);
            }
        }

        _GLIBCXX20_CONSTEXPR pointer InsertRaw(pointer at, size_type count) {
            if (count == 0)
                return at;

            size_type vsize     = size();
            size_type vcapacity = capacity();

            if (vcapacity < count + vsize) {
                size_type ncapacity = Max(mNextResize + mGrowthFactor, count + vsize);

                // Allocate the new buffer since we maxxed the capacity
                pointer nbuffer = mAllocator.allocate(ncapacity);
                if (!nbuffer)
                    return mEnd;

                // Copy construct the values before the insertion
                pointer nbiter = nbuffer;
                for (pointer i = mBegin; i != at; ++i) {
#if __cplusplus <= 201703L
                    mAllocator.construct(nbiter++, *i);
#else
                    mAllocator.construct_at(nbiter++, *i);
#endif
                }

                // Copy construct the values after the insertion
                pointer naiter = nbiter + count;
                for (pointer i = at; i != mEnd; ++i) {
#if __cplusplus <= 201703L
                    mAllocator.construct(naiter++, *i);
#else
                    mAllocator.construct_at(naiter++, *i);
#endif
                }

                // Destroy old buffer
                DestroyElement_(mBegin, mEnd);

                pointer vbegin = mBegin;

                mEnd      = nbuffer + count + vsize;
                mBegin    = nbuffer;
                mCapacity = ncapacity;

                mAllocator.deallocate(vbegin, 1);
                return nbiter;
            }

            pointer ieiter = at + count;
            if (ieiter < mEnd) {
                // Copy construct the values after the end
                pointer newiter = mEnd;
                for (pointer i = mEnd - count; i != mEnd; ++i) {
#if __cplusplus <= 201703L
                    mAllocator.construct(newiter++, *i);
#else
                    mAllocator.construct_at(newiter++, *i);
#endif
                }

                // Copy new values into inserted space? (Makes no sense)
                pointer insertIter = mEnd;
                for (pointer i = mEnd - count; i > at; --i) {
                    *insertIter = *i;
                }

                for (pointer i = at; i != mEnd - count; ++i) {
                    mAllocator.destroy(i);
                }
            } else {
                pointer insertIter = ieiter;
                for (pointer i = at; i != mEnd; ++i) {
#if __cplusplus <= 201703L
                    mAllocator.construct(insertIter++, *i);
#else
                    mAllocator.construct_at(insertIter++, *i);
#endif
                }

                // Destroy old elements
                DestroyElement_(at, mEnd);
            }

            mEnd += count;
            return at;
        }

    private:
        allocator_type mAllocator;
        pointer mBegin;
        pointer mEnd;
        size_type mCapacity;
        f32 mGrowthFactor;
        size_type mNextResize;
    };

    template <class _T, class _Alloc>
    _GLIBCXX20_CONSTEXPR bool operator==(const TVector<_T, _Alloc> &lhs,
                                         const TVector<_T, _Alloc> &rhs) {
        if (lhs.size() != rhs.size())
            return false;

        auto i = lhs.begin();
        auto j = rhs.begin();
        while (i != lhs.end() && j != rhs.end()) {
            if (i++ != j++)
                return false;
        }

        return true;
    }

#if __cplusplus <= 201703L
    template <class _T, class _Alloc>
    bool operator!=(const TVector<_T, _Alloc> &lhs, const TVector<_T, _Alloc> &rhs) {
        if (lhs.size() != rhs.size())
            return true;

        auto i = lhs.begin();
        auto j = rhs.begin();
        while (i != lhs.end() && j != rhs.end()) {
            if (i++ != j++)
                return true;
        }

        return false;
    }
#endif

    class TVector_pointer_void : public TVector<void *> {
        TVector_pointer_void();
        ~TVector_pointer_void();

        iterator insert(void **a, void **b);
        void reserve(size_type capacity);
    };

}  // namespace JGadget