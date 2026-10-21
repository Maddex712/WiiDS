#pragma once

#include <Dolphin/OS.h>
#include <Dolphin/math.h>
#include <Dolphin/mem.h>
#include <Dolphin/types.h>

#include <JSystem/JDrama/JDRNameRef.hxx>
#include <JSystem/JGadget/Allocator.hxx>
#include <JSystem/JGadget/Pair.hxx>
#include <JSystem/bits/functional_hash.h>
#include <JSystem/function.hxx>
#include <JSystem/initializer_list.hxx>

namespace JGadget {
#define _JGADGET_MAP_DEFAULT_BUCKETS 256

    template <class _Key, class _T, class _Hash = JSystem::hash<_Key>,
              class _Pred  = JSystem::equal_to<_Key>,
              class _Alloc = TAllocator<TPair<const _Key, _T>>>
    class TUnorderedMap {
    public:
        typedef _Key key_type;
        typedef _T mapped_type;
        typedef TPair<const key_type, mapped_type> value_type;
        typedef _Hash hasher;
        typedef _Pred key_equal;
        typedef _Alloc allocator_type;
        typedef typename _Alloc::reference reference;
        typedef typename _Alloc::const_reference const_reference;
        typedef typename _Alloc::pointer pointer;
        typedef typename _Alloc::const_pointer const_pointer;
        typedef size_t size_type;
        typedef ptrdiff_t difference_type;

    private:
        struct TNode_ {
            TNode_ *mNext;
            value_type mValue;

            bool operator==(const TNode_ &other) const { return mValue == other.mValue; }
            bool operator!=(const TNode_ &other) const { return mValue != other.mValue; }
        };

        typedef typename _Alloc::template rebind<value_type>::other pair_allocator_t;
        typedef typename _Alloc::template rebind<TNode_>::other node_allocator_t;
        typedef typename _Alloc::template rebind<TNode_ *>::other bucket_allocator_t;

    public:
        struct const_iterator;

        struct iterator {
            typedef _T value_type;
            typedef ptrdiff_t difference_type;
            typedef typename _Alloc::pointer pointer;
            typedef typename _Alloc::const_pointer const_pointer;

            friend class TUnorderedMap;
            friend struct TUnorderedMap::const_iterator;

            iterator(const iterator &iter) = default;

        private:
            iterator(TNode_ *node, TNode_ **bucket) : mElement(node), mBucket(bucket) {}
            iterator(TNode_ **bucket) : mElement(*bucket), mBucket(bucket) {}
            explicit iterator(const const_iterator &iter)
                : mElement(const_cast<TNode_ *>(iter.mElement)) {}

        public:
            bool operator==(const iterator &rhs) const { return mElement == rhs.mElement; }
            bool operator!=(const iterator &rhs) const { return mElement != rhs.mElement; }

            iterator &operator++() {
                next();
                return *this;
            }

            iterator operator++(int) {
                iterator temp{mElement, mBucket};
                next();
                return temp;
            }

            pointer operator->() const { return JSystem::addressof(mElement->mValue); }
            reference operator*() { return mElement->mValue; }

        private:
            void next() {
                mElement = mElement->mNext;
                if (!mElement) {
                    next_bucket();
                }
            }

            void next_bucket() {
                ++mBucket;
                while (!*mBucket)  // Requires sentinel
                    ++mBucket;
                mElement = *mBucket;
            }

            TNode_ *mElement;
            TNode_ **mBucket;
        };

        struct const_iterator {
            typedef _T value_type;
            typedef ptrdiff_t difference_type;
            typedef typename _Alloc::pointer pointer;
            typedef typename _Alloc::const_pointer const_pointer;

            friend class TUnorderedMap;
            friend struct TUnorderedMap::iterator;

            const_iterator(const const_iterator &iter) = default;

        private:
            const_iterator(TNode_ *node, TNode_ **bucket) : mElement(node), mBucket(bucket) {}
            const_iterator(TNode_ **bucket) : mElement(*bucket), mBucket(bucket) {}
            const_iterator(const iterator &iter) : mElement(const_cast<TNode_ *>(iter.mElement)) {}

        public:
            bool operator==(const const_iterator &rhs) const { return mElement == rhs.mElement; }
            bool operator!=(const const_iterator &rhs) const { return mElement != rhs.mElement; }

            const_iterator &operator++() {
                next();
                return *this;
            }

            const_iterator operator++(int) {
                const_iterator temp{mElement, mBucket};
                next();
                return temp;
            }

            const_pointer operator->() const { return JSystem::addressof(mElement->mValue); }
            const_reference operator*() { return mElement->mValue; }

        private:
            void next() {
                mElement = mElement->mNext;
                if (!mElement) {
                    next_bucket();
                }
            }

            void next_bucket() {
                ++mBucket;
                while (!*mBucket)  // Requires sentinel
                {
                    ++mBucket;
                }
                mElement = *mBucket;
            }

            TNode_ *mElement;
            TNode_ **mBucket;
        };

        struct const_local_iterator;

        struct local_iterator {
            typedef _T value_type;
            typedef ptrdiff_t difference_type;
            typedef typename _Alloc::pointer pointer;
            typedef typename _Alloc::const_pointer const_pointer;

            friend class TUnorderedMap;
            friend struct TUnorderedMap::const_local_iterator;

            local_iterator(const local_iterator &iter) = default;

        private:
            local_iterator(TNode_ *node) : mElement(node) {}
            explicit local_iterator(const const_local_iterator &iter)
                : mElement(const_cast<TNode_ *>(iter.mElement)) {}

        public:
            bool operator==(const local_iterator &rhs) const { return mElement == rhs.mElement; }
            bool operator!=(const local_iterator &rhs) const { return mElement != rhs.mElement; }

            local_iterator &operator++() {
                mElement = mElement->mNext;
                return *this;
            }

            local_iterator operator++(int) {
                local_iterator temp{mElement};
                mElement = mElement->mNext;
                return temp;
            }

            pointer operator->() const { return JSystem::addressof(mElement->mValue); }
            reference operator*() { return mElement->mValue; }

        private:
            TNode_ *mElement;
        };

        struct const_local_iterator {
            typedef _T value_type;
            typedef ptrdiff_t difference_type;
            typedef typename _Alloc::pointer pointer;
            typedef typename _Alloc::const_pointer const_pointer;

            friend class TUnorderedMap;
            friend struct TUnorderedMap::local_iterator;

            const_local_iterator(const const_local_iterator &iter) = default;

        private:
            const_local_iterator(TNode_ *node) : mElement(node) {}
            const_local_iterator(const local_iterator &iter)
                : mElement(const_cast<TNode_ *>(iter.mElement)) {}

        public:
            bool operator==(const const_local_iterator &rhs) const {
                return mElement == rhs.mElement;
            }
            bool operator!=(const const_local_iterator &rhs) const {
                return mElement != rhs.mElement;
            }

            const_local_iterator &operator++() {
                mElement = mElement->mNext;
                return *this;
            }

            const_local_iterator operator++(int) {
                const_local_iterator temp{mElement};
                mElement = mElement->mNext;
                return temp;
            }

            const_pointer operator->() const { return JSystem::addressof(mElement->mValue); }
            const_reference operator*() { return mElement->mValue; }

        private:
            TNode_ *mElement;
        };

        TUnorderedMap() : TUnorderedMap(_JGADGET_MAP_DEFAULT_BUCKETS) {}
        // TUnorderedMap() = delete;

        explicit TUnorderedMap(size_t buckets, const hasher &hf = hasher(),
                               const key_equal &ke         = key_equal(),
                               const allocator_type &alloc = allocator_type())
            : mNodeAllocator(alloc), mHasher(hf), mKeyEqual(ke), mBuckets(nullptr),
              mBucketCount(buckets), mElementCount(0), mMaxLoadFactor(1.0f) {
            mBuckets = allocate_buckets(buckets);
        }

        TUnorderedMap(size_t buckets, const allocator_type &alloc)
            : TUnorderedMap(buckets, hasher(), key_equal(), alloc) {}

        TUnorderedMap(size_t buckets, const hasher &hf, const allocator_type &alloc)
            : TUnorderedMap(buckets, hf, key_equal(), alloc) {}

        TUnorderedMap(const TUnorderedMap &ump) : TUnorderedMap(ump, ump.mNodeAllocator) {}

        TUnorderedMap(const TUnorderedMap &ump, const allocator_type &alloc)
            : TUnorderedMap(ump.mBucketCount, ump.mHasher, ump.mKeyEqual, ump.mNodeAllocator) {
            for (size_type i = 0; i < ump.mBucketCount; ++i) {
                TNode_ *n     = ump.mBuckets[i];
                TNode_ **tail = mBuckets + i;
                while (n) {
                    *tail = allocate_node(n->mValue);
                    tail  = &((*tail)->mNext);
                    n     = n->mNext;
                }
            }
        }

#if __cplusplus >= 201103L
        TUnorderedMap(JSystem::initializer_list<value_type> list,
                      size_t buckets = _JGADGET_MAP_DEFAULT_BUCKETS, const hasher &hf = hasher(),
                      const key_equal &ke         = key_equal(),
                      const allocator_type &alloc = allocator_type())
            : TUnorderedMap(buckets, hf, ke, alloc) {
            for (auto &i : list) {
                insert(i);
            }
        }

#if __cplusplus >= 201402L
        TUnorderedMap(JSystem::initializer_list<value_type> list, size_t buckets,
                      const allocator_type &alloc)
            : TUnorderedMap(list, buckets, hasher(), key_equal(), alloc) {}

        TUnorderedMap(JSystem::initializer_list<value_type> list, size_t buckets, const hasher &hf,
                      const allocator_type &alloc)
            : TUnorderedMap(list, buckets, hf, key_equal(), alloc) {}
#endif
#endif

        TUnorderedMap &operator=(const TUnorderedMap &other) {
            clear();

            mBucketCount  = other.mBucketCount;
            mElementCount = other.mElementCount;
            for (size_type i = 0; i < other.mBucketCount; ++i) {
                TNode_ *n     = other.mBuckets;
                TNode_ **tail = mBuckets + i;
                while (n) {
                    *tail = allocate_node(n->mValue);
                    tail  = &((*tail)->mNext);
                    n     = n->mNext;
                }
            }

            return *this;
        }

        ~TUnorderedMap() {
            clear();
            deallocate_buckets(mBuckets, mBucketCount);
        }

        allocator_type get_allocator() const _GLIBCXX_NOEXCEPT { return mNodeAllocator; }

        iterator begin() _GLIBCXX_NOEXCEPT {
            iterator i(mBuckets);
            if (!i.mElement)
                i.next_bucket();
            return i;
        }
        const_iterator begin() const _GLIBCXX_NOEXCEPT {
            const_iterator i(mBuckets);
            if (!i.mElement)
                i.next_bucket();
            return i;
        }
        iterator end() _GLIBCXX_NOEXCEPT { return iterator(mBuckets + mBucketCount); }
        const_iterator end() const _GLIBCXX_NOEXCEPT {
            return const_iterator(mBuckets + mBucketCount);
        }

#if __cplusplus >= 201103L
        const_iterator cbegin() const _GLIBCXX_NOEXCEPT {
            const_iterator i(mBuckets);
            if (!i.mElement)
                i.next_bucket();
            return i;
        }
        const_iterator cend() const _GLIBCXX_NOEXCEPT {
            return const_iterator(mBuckets + mBucketCount);
        }
#endif

        _GLIBCXX_NODISCARD bool empty() const _GLIBCXX_NOEXCEPT { return begin() == end(); }

        size_type max_size() const { return difference_type(-1) / sizeof(difference_type); }
        size_type size() const { return mElementCount; }

        void clear() _GLIBCXX_NOEXCEPT {
            OSReport("clear\n");
            deallocate_nodes(mBuckets, mBucketCount);
            mElementCount = 0;
        }

        local_iterator begin(size_type bucket) { return local_iterator(mBuckets[bucket]); }
        const_local_iterator begin(size_type bucket) const {
            return const_local_iterator(mBuckets[bucket]);
        }
        local_iterator end(size_type bucket) { return local_iterator(nullptr); }
        const_local_iterator end(size_type bucket) const { return const_local_iterator(nullptr); }

#if __cplusplus >= 201103L
        const_local_iterator cbegin(size_type bucket) const {
            return const_local_iterator(mBuckets[bucket]);
        }
        const_local_iterator cend(size_type bucket) const { return const_local_iterator(nullptr); }
#endif

        size_type bucket_count() const { return mBucketCount; }

        size_type max_bucket_count() const { return difference_type(-1) / sizeof(difference_type); }

        size_type bucket_size(size_type bucket) const {
            size_type n;
            for (auto i = begin(bucket); i != end(bucket); ++i) {
                ++n;
            }
            return n;
        }

        size_type bucket(const key_type &key) const { return bucket_index(key, bucket_count()); }

        f32 load_factor() const { return static_cast<f32>(size()) / bucket_count(); }

        f32 max_load_factor() const { return mMaxLoadFactor; }
        void max_load_factor(f32 factor) { mMaxLoadFactor = factor; }

        TPair<iterator, bool> insert(const value_type &value) {
            size_type n = bucket(value.first);
            if (TNode_ *p = find_node(mBuckets[n], value.first))
                return {iterator(p, mBuckets + n), false};
            return {insert_bucket(value, n), true};
        }

        TPair<iterator, bool> insert(value_type &&value) {
            size_type n = bucket(value.first);
            if (TNode_ *p = find_node(mBuckets[n], value.first))
                return {iterator(p, mBuckets + n), false};
            return {insert_bucket(value, n), true};
        }

#if __cplusplus >= 201703L
        TPair<iterator, bool> insert_or_assign(const key_type &key, mapped_type &&value) {
            size_type n = bucket(key);
            if (TNode_ *p = find_node(mBuckets[n], key)) {
                p->mValue.second = value;
                return {iterator(p, mBuckets + n), false};
            }
            return {insert_bucket(value_type(key, value), n), true};
        }

        TPair<iterator, bool> insert_or_assign(key_type &&key, mapped_type &&value) {
            size_type n = bucket(key);
            if (TNode_ *p = find_node(mBuckets[n], key)) {
                p->mValue.second = value;
                return {iterator(p, mBuckets + n), false};
            }
            return {insert_bucket(value_type(key, value), n), true};
        }
#endif

        template <class... Args> TPair<iterator, bool> emplace(Args &&...args) {
            value_type value = value_type(JSystem::forward<Args>(args)...);

            size_type n = bucket(value.first);
            if (TNode_ *p = find_node(mBuckets[n], value.first))
                return {iterator(p, mBuckets + n), false};
            return {insert_bucket(value, n), true};
        }

        iterator erase(const_iterator pos) {
            iterator result = pos;
            result++;
            erase_node(pos.mElement, pos.mBucket);
        }

        iterator erase(const_iterator first, const_iterator last) {
            while (first != last) {
                first = erase(first);
            }
            return last;
        }

        size_type erase(const key_type &key) {
            typename hasher::result_type hash = get_hash(key);
            size_type n                       = bucket(key);

            TNode_ **slot = mBuckets + n;
            while (*slot && !mKeyEqual(key, (*slot)->mValue.first))
                slot = &((*slot)->mNext);

            if (!*slot)
                return 0;

            TNode_ *p = *slot;
            *slot     = p->mNext;
            deallocate_node(p);
            --mElementCount;

            return 1;
        }

        mapped_type &at(const key_type &key) {
            TNode_ *p = find_node(key);
            if (!p) {
                OSPanic(__FILE__, __LINE__, "Unordered Map lookup failed, no such key exists!");
                __OSUnhandledException(6, OSGetCurrentContext(), 0);
            }
            return p->mValue.second;
        }

        const mapped_type &at(const key_type &key) const {
            TNode_ *p = find_node(key);
            if (!p) {
                OSPanic(__FILE__, __LINE__, "Unordered Map lookup failed, no such key exists!");
                __OSUnhandledException(6, OSGetCurrentContext(), 0);
            }
            return p->mValue.second;
        }

        mapped_type &operator[](const key_type &key) {
            size_type n = bucket(key);
            TNode_ *p   = find_node(mBuckets[n], key);
            if (!p)
                return insert_bucket(value_type(key, mapped_type()), n)->second;
            return (p->mValue).second;
        }

        mapped_type &operator[](key_type &&key) {
            size_type n = bucket(key);
            TNode_ *p   = find_node(mBuckets[n], key);
            if (!p)
                return insert_bucket(value_type(key, mapped_type()), n)->second;
            return (p->mValue).second;
        }

        size_type count(const key_type &key) {
            TNode_ *p = find_node(key);
            return p ? 1 : 0;
        }

        iterator find(const key_type &key) {
            size_type n = bucket(key);
            TNode_ *p   = find_node(mBuckets[n], key);
            return p ? iterator(p, mBuckets + n) : end();
        }

        const_iterator find(const key_type &key) const {
            size_type n = bucket(key);
            TNode_ *p   = find_node(mBuckets[n], key);
            return p ? const_iterator(p, mBuckets + n) : end();
        }

#if __cplusplus > 201703L
        bool contains(const key_type &key) {
            size_type n = bucket(key);
            return find_node(mBuckets[n], key) != nullptr;
        }
#endif

        TPair<iterator, iterator> equal_range(const key_type &key) {
            auto i = find(key);
            if (i == end())
                return {end(), end()};
            return {i, i + 1};
        }

        void rehash(size_type count) {
            size_type next = static_cast<f32>(size()) / max_load_factor();
            if (count < next)
                count = next;

            TNode_ **nbkts = allocate_buckets(count);

            for (size_type i = 0; i < mBucketCount; ++i) {
                while (TNode_ *p = mBuckets[i]) {
                    size_type nidx = bucket_index(p->mValue.first, count);
                    mBuckets[i]    = p->mNext;
                    p->mNext       = nbkts[nidx];
                    nbkts[nidx]    = p;
                }
            }
            deallocate_buckets(mBuckets, mBucketCount);
            mBucketCount = count;
            mBuckets     = nbkts;
        }

        void resize(size_type count) { rehash(int((count / max_load_factor()) + 1.0f)); }

        hasher hash_function() const { return mHasher; }
        hasher key_eq() const { return mKeyEqual; }

    private:
        size_type bucket_index(const key_type &key, size_type n) const { return get_hash(key) % n; }
        typename hasher::result_type get_hash(const key_type &key) const { return mHasher(key); }
        typename hasher::result_type get_hash(key_type &&key) const { return mHasher(key); }

        void erase_node(TNode_ *p, TNode_ **b) {
            TNode_ *cur = *b;
            if (cur == p)
                *b = cur->mNext;
            else {
                TNode_ *next = cur->mNext;
                while (next != p) {
                    cur  = next;
                    next = cur->mNext;
                }
                cur->mNext = next->mNext;
            }
            deallocate_node(p);
            --mElementCount;
        }

        TNode_ *find_node(const key_type &key) {
            size_type n = bucket(key);
            return find_node(mBuckets[n], key);
        }

        TNode_ *find_node(TNode_ *p, const key_type &key) {
            for (; p; p = p->mNext) {
                if (mKeyEqual(p->mValue.first, key))
                    return p;
            }
            return nullptr;
        }

        TNode_ *allocate_node(const value_type &v) {
            TNode_ *n = mNodeAllocator.allocate(1);
#if __cplusplus <= 201703L
            get_allocator().construct(&n->mValue, v);
#else
            get_allocator().construct_at(&n->mValue, v);
#endif
            n->mNext = nullptr;
            return n;
        }

        void deallocate_node(TNode_ *n) {
            pair_allocator_t alloc(mNodeAllocator);
            // alloc.destroy(&n->mValue);
            alloc.destroy(&n->mValue.first);
            alloc.destroy(&n->mValue.second);
            mNodeAllocator.deallocate(n, 1);
        }

        void deallocate_nodes(TNode_ **array, size_type n) {
            for (size_type i = 0; i < n; ++i) {
                TNode_ *p = array[i];
                while (p) {
                    TNode_ *tmp = p;
                    p           = p->mNext;
                    deallocate_node(tmp);
                }
                array[i] = nullptr;
            }
        }

        TNode_ **allocate_buckets(size_type n) {
            bucket_allocator_t alloc(mNodeAllocator);
            TNode_ **p = alloc.allocate(n + 1);  // Add 1 for sentinel
            memset(p, 0, sizeof(p) * n);
            p[n] = reinterpret_cast<TNode_ *>(0x1000);
            return p;
        }

        void deallocate_buckets(TNode_ **p, size_type n) {
            bucket_allocator_t alloc(mNodeAllocator);
            alloc.deallocate(p, n + 1);
        }

        iterator insert_bucket(const value_type &v, size_type n) {
            TNode_ *newnode = allocate_node(v);

            if (load_factor() > max_load_factor()) {
                n = bucket_index(v.first, bucket_count() * 2);
                rehash(bucket_count() * 2);
            }

            newnode->mNext = mBuckets[n];
            mBuckets[n]    = newnode;
            ++mElementCount;
            return iterator(newnode, mBuckets + n);
        }

        node_allocator_t mNodeAllocator;
        hasher mHasher;
        key_equal mKeyEqual;
        TNode_ **mBuckets;
        size_type mBucketCount;
        size_type mElementCount;
        f32 mMaxLoadFactor;
    };

    template <class _Key, class _T, class _Hash, class _Pred, class _Alloc>
    bool operator==(const TUnorderedMap<_Key, _T, _Hash, _Pred, _Alloc> &a,
                    const TUnorderedMap<_Key, _T, _Hash, _Pred, _Alloc> &b) {
        if (a.size() != b.size())
            return false;

        for (auto i = a.begin(); i != a.end(); ++i) {
            auto j = b.find(i->first);
            if (j == b.end() || *i != *j)
                return false;
        }
    }

#undef _JGADGET_MAP_DEFAULT_BUCKETS
}  // namespace JGadget
