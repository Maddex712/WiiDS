#pragma once

#include <Dolphin/OS.h>
#include <Dolphin/types.h>

#include <JSystem/JGadget/Allocator.hxx>
#include <JSystem/JGadget/Node.hxx>
#include <JSystem/JKernel/JKRHeap.hxx>
#include <JSystem/initializer_list.hxx>
#include <JSystem/utility.hxx>

namespace JGadget {

    namespace {
        template <typename _T> class TPRIsEqual_Pointer_ {
            _T *pointer;

        public:
            TPRIsEqual_Pointer_() = delete;
            TPRIsEqual_Pointer_(_T *p) : pointer(p) {}

            bool operator()(const TPRIsEqual_Pointer_ &rhs) const { return pointer == rhs.pointer; }
        };
    }  // namespace

    template <class _T, class _Alloc = TAllocator<_T>> class TList {
    public:
        typedef _T value_type;
        typedef _Alloc allocator_type;
#if __SIZEOF_POINTER__ == 8
        typedef u32 size_type;
#else
        typedef size_t size_type;
#endif
        typedef ptrdiff_t difference_type;
        typedef value_type &reference;
        typedef const value_type &const_reference;
        typedef typename _Alloc::pointer pointer;
        typedef typename _Alloc::const_pointer const_pointer;

        struct const_iterator;

    private:
        struct TNode_ {
            TNode_ *mNext;
            TNode_ *mPrev;
            value_type mItem;

            TNode_(TNode_ *next, TNode_ *prev, const value_type &item)
                : mNext(next), mPrev(prev), mItem(item) {}
#if __cplusplus >= 201103L
            TNode_(TNode_ *next, TNode_ *prev, value_type &&item)
                : mNext(next), mPrev(prev), mItem(item) {}
#endif
            ~TNode_() {}
        };

        TNode_ *CreateNode_(TNode_ *next, TNode_ *prev, const value_type &item) {
            TNode_ *p = mNodeAllocator.allocate(1);
#if __cplusplus < 201103L
            mNodeAllocator.construct(p, TNode_(next, prev, item));
#elif __cplusplus <= 201703L
            mNodeAllocator.construct(p, next, prev, item);
#else
            mNodeAllocator.construct_at(p, next, prev, item);
#endif
            return p;
        }

        typedef typename _Alloc::template rebind<TNode_>::other node_allocator_t;

    public:
        struct iterator {
            typedef _T value_type;
            typedef ptrdiff_t difference_type;
            typedef typename _Alloc::pointer pointer;
            typedef typename _Alloc::const_pointer const_pointer;

            friend class TList;
            friend struct TList::const_iterator;

            iterator(const iterator &iter) = default;

        private:
            iterator(TNode_ *node) : mNode(node) {}
            explicit iterator(const const_iterator &iter)
                : mNode(const_cast<TNode_ *>(iter.mNode)) {}

        public:
            bool operator==(const iterator &rhs) const { return mNode == rhs.mNode; }
            bool operator!=(const iterator &rhs) const { return mNode != rhs.mNode; }

            iterator operator+(int i) {
                iterator temp{mNode};
                for (size_t k = 0; k < i; ++k) {
                    temp->mNode = temp->mNode->mNext;  // Undefined behavior if past end
                }
                return temp;
            }

            iterator &operator+=(int i) {
                for (size_t k = 0; k < i; ++k) {
                    mNode = mNode->mNext;  // Undefined behavior if past end
                }
                return *this;
            }

            iterator &operator++() {
                mNode = mNode->mNext;
                return *this;
            }

            iterator operator++(int) {
                iterator temp{mNode};
                mNode = mNode->mNext;
                return temp;
            }

            iterator operator-(int i) {
                iterator temp{mNode};
                for (size_t k = 0; k < i; ++k) {
                    temp->mNode = temp->mNode->mPrev;  // Undefined behavior if past begin
                }
                return temp;
            }

            iterator &operator-=(int i) {
                for (size_t k = 0; k < i; ++k) {
                    mNode = mNode->mPrev;  // Undefined behavior if past begin
                }
                return *this;
            }

            iterator &operator--() {
                mNode = mNode->mPrev;
                return *this;
            }

            iterator operator--(int) {
                iterator temp{mNode};
                mNode = mNode->mPrev;
                return temp;
            }

            pointer operator->() const { return JSystem::addressof(mNode->mItem); }
            reference operator*() { return mNode->mItem; }

        private:
            TNode_ *mNode;
        };

        struct const_iterator {
            typedef _T value_type;
            typedef ptrdiff_t difference_type;
            typedef typename _Alloc::pointer pointer;
            typedef typename _Alloc::const_pointer const_pointer;

            friend class TList;
            friend struct TList::iterator;

            const_iterator(const iterator &iter) : mNode(iter.mNode) {}
            const_iterator(const const_iterator &iter) = default;

        private:
            explicit const_iterator(const TNode_ *node) : mNode(node) {}

        public:
            bool operator==(const const_iterator &rhs) const { return mNode == rhs.mNode; }
            bool operator!=(const const_iterator &rhs) const { return mNode != rhs.mNode; }

            const_iterator operator+(int i) {
                const_iterator temp{mNode};
                for (size_t k = 0; k < i; ++k) {
                    temp->mNode = temp->mNode->mNext;  // Undefined behavior if past end
                }
                return temp;
            }

            const_iterator &operator+=(int i) {
                for (size_t k = 0; k < i; ++k) {
                    mNode = mNode->mNext;  // Undefined behavior if past end
                }
                return *this;
            }

            const_iterator &operator++() {
                mNode = mNode->mNext;
                return *this;
            }

            const_iterator operator++(int) {
                const_iterator temp{mNode};
                mNode = mNode->mNext;
                return temp;
            }

            const_iterator operator-(int i) {
                const_iterator temp{mNode};
                for (size_t k = 0; k < i; ++k) {
                    temp->mNode = temp->mNode->mPrev;  // Undefined behavior if past begin
                }
                return temp;
            }

            const_iterator &operator-=(int i) {
                for (size_t k = 0; k < i; ++k) {
                    mNode = mNode->mPrev;  // Undefined behavior if past begin
                }
                return *this;
            }

            const_iterator &operator--() {
                mNode = mNode->mPrev;
                return *this;
            }

            const_iterator operator--(int) {
                iterator temp{mNode};
                mNode = mNode->mPrev;
                return temp;
            }

            const_pointer operator->() const { return JSystem::addressof(mNode->mItem); }
            const_reference &operator*() { return mNode->mItem; }

        private:
            const TNode_ *mNode;
        };

        TList() : mNodeAllocator(), mSize(0), mBegin(nullptr), mEnd(nullptr) {
            mBegin = reinterpret_cast<TNode_ *>(&mBegin);
            mEnd   = reinterpret_cast<TNode_ *>(&mBegin);
        }

        explicit TList(const allocator_type &allocator) : TList() { mNodeAllocator = allocator; }

#if __cplusplus >= 201103L
        TList(size_type count, const value_type &value,
              const allocator_type &allocator = allocator_type())
            : TList(allocator) {
            insert(end(), count, value);
        }
#else
        explicit TList(size_type count, const value_type &value = value_type(),
                       const allocator_type &allocator = allocator_type())
            : TList(allocator) {
            insert(end(), count, value);
        }
#endif

#if __cplusplus >= 201402L
        explicit TList(size_type count, const allocator_type &allocator = allocator_type())
            : TList(allocator) {
            insert(end(), count, value_type());
        }
#else
        explicit TList(size_type count) : TList { insert(end(), count, value_type()); }
#endif

        TList(const TList &other) : TList(other.mNodeAllocator) {
            for (auto i : other) {
                insert(end(), i);
            }
        }

#if __cplusplus >= 201103L
        TList(const TList &other, const allocator_type &allocator) : TList(allocator) {
            for (auto &item : other) {
                insert(end(), item);
            }
        }

        TList(TList &&other) : TList(other.mNodeAllocator) {
            for (auto &item : other) {
                insert(end(), item);
            }
        }

        TList(TList &&other, const allocator_type &allocator) : TList(allocator) {
            for (auto &item : other) {
                insert(end(), item);
            }
        }

        TList(JSystem::initializer_list<value_type> list,
              const allocator_type &allocator = allocator_type())
            : TList(allocator) {
            for (auto &i : list) {
                insert(end(), i);
            }
        }
#endif

        ~TList() { clear(); }

        TList &operator=(const TList &other) {
            clear();
            for (const auto &i : other) {
                insert(end(), i);
            }
            return *this;
        }

#if __cplusplus >= 201103L
        TList &operator=(TList &&other) {
            clear();
            for (const auto &i : other) {
                insert(end(), i);
            }
            return *this;
        }

        TList &operator=(JSystem::initializer_list<value_type> list) {
            clear();
            for (const auto &i : list) {
                insert(end(), i);
            }
            return *this;
        }
#endif

        _GLIBCXX20_CONSTEXPR allocator_type get_allocator() const { return mNodeAllocator; }

        reference front() { return *begin(); }
        const_reference front() const { return *begin(); }

        reference back() {
            iterator tmp = end();
            --tmp;
            return *tmp;
        }
        const_reference back() const {
            iterator tmp = end();
            --tmp;
            return *tmp;
        }

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

        _GLIBCXX_NODISCARD bool empty() const _GLIBCXX_NOEXCEPT {
            return mBegin == mEnd && mSize == 0;
        }
        void clear() _GLIBCXX_NOEXCEPT { erase(begin(), end()); }

        size_type max_size() const _GLIBCXX_NOEXCEPT {
            return difference_type(-1) / sizeof(difference_type);
        }
        size_t size() const _GLIBCXX_NOEXCEPT { return mSize; }

        iterator begin() _GLIBCXX_NOEXCEPT { return iterator(mBegin); }
        const_iterator begin() const _GLIBCXX_NOEXCEPT { return const_iterator(mBegin); }
        iterator end() _GLIBCXX_NOEXCEPT { return iterator(reinterpret_cast<TNode_ *>(&mBegin)); }
        const_iterator end() const _GLIBCXX_NOEXCEPT {
            return const_iterator(reinterpret_cast<const TNode_ *>(&mBegin));
        }

#if __cplusplus >= 201103L
        const_iterator cbegin() const _GLIBCXX_NOEXCEPT { return {mBegin}; }
        const_iterator cend() const _GLIBCXX_NOEXCEPT {
            return {reinterpret_cast<const TNode_ *>(&mBegin)};
        }
#endif

#if __cplusplus >= 201103L
        iterator erase(const_iterator iter) {
            TNode_ *next = iter.mNode->mNext;
            TNode_ *prev = iter.mNode->mPrev;

            prev->mNext = next;
            next->mPrev = prev;

            get_allocator().destroy(&iter.mNode->mItem);
            delete iter.mNode;

            mSize -= 1;
            return {next};
        }

        iterator erase(const_iterator start, const_iterator end) {
            iterator iter = iterator(start);
            while (iter != iterator(end)) {
                iter = erase(iter);
            }
            return iter;
        }
#else
        iterator erase(iterator iter) {
            TNode_ *next = iter.mNode->mNext;
            TNode_ *prev = iter.mNode->mPrev;

            prev->mNext = next;
            next->mPrev = prev;

            get_allocator().destroy(&iter.mNode->mItem);
            delete iter.mNode;

            mSize -= 1;
            return {next};
        }

        iterator erase(iterator start, iterator end) {
            iterator iter = start;
            while (iter != end) {
                iter = erase(iter);
            }
            return iter;
        }
#endif

        iterator insert(const_iterator at, const value_type &value) {
            TNode_ *current = const_cast<TNode_ *>(at.mNode);
            TNode_ *prev    = current->mPrev;

            TNode_ *newNode = CreateNode_(current, prev, value);
            if (!newNode)
                return mEnd;

            current->mPrev = newNode;
            prev->mNext    = newNode;
            mSize += 1;

            return {newNode};
        }

        iterator insert(const_iterator at, value_type &&value) {
            value_type tmp = value;

            TNode_ *current = const_cast<TNode_ *>(at.mNode);
            TNode_ *prev    = current->mPrev;

            TNode_ *newNode = CreateNode_(current, prev, tmp);
            if (!newNode)
                return mEnd;

            current->mPrev = newNode;
            prev->mNext    = newNode;
            mSize += 1;

            return {newNode};
        }

        iterator insert(const_iterator at, size_type count, const value_type &value) {
            for (size_t i = 0; i < count; ++i) {
                at = insert(at, value);
            }
            return at;
        }

        iterator insert(const_iterator at, size_type count, value_type &&value) {
            value_type tmp = value;
            for (size_t i = 0; i < count; ++i) {
                at = insert(at, tmp);
            }
            return at;
        }

#if __cplusplus >= 201103L
        iterator insert(const_iterator at, JSystem::initializer_list<value_type> list) {
            for (auto &i : list) {
                at = insert(at, i);
            }
            return at;
        }

        template <class... _Args> iterator emplace(const_iterator at, _Args &&...args) {
            value_type tmp;

#if __cplusplus <= 201703L
            get_allocator().construct(tmp, JSystem::forward<_Args>(args)...);
#else
            get_allocator().construct_at(tmp, JSystem::forward<_Args>(args)...);
#endif

            return insert(at, tmp);
        }
#endif

#if __cplusplus >= 201703L
        template <class... _Args> reference emplace_back(_Args &&...args) {
            return *emplace(end(), JSystem::forward<_Args>(args)...);
        }
        template <class... _Args> reference emplace_front(_Args &&...args) {
            return *emplace(begin(), JSystem::forward<_Args>(args)...);
        }
#else
        template <class... _Args> void emplace_back(_Args &&...args) {
            emplace(end(), JSystem::forward<_Args>(args)...);
        }
        template <class... _Args> void emplace_front(_Args &&...args) {
            emplace(begin(), JSystem::forward<_Args>(args)...);
        }
#endif

        void pop_back() {
            if (empty())
                return;
            erase(--end());
        }

        void pop_front() {
            if (empty())
                return;
            erase(begin());
        }

        void push_back() {
            if (empty())
                return;
            erase(--end());
        }

        void push_back(const value_type &value) { insert(end(), value); }
        void push_front(const value_type &value) { insert(begin(), value); }

#if __cplusplus >= 201103L
        void push_back(value_type &&value) { insert(end(), value); }
        void push_front(value_type &&value) { insert(begin(), value); }
#endif

        void resize(size_type s) { resize(s, value_type()); }

#if __cplusplus >= 201103L
        void resize(size_type s, const value_type &value) {
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
        void resize(size_type s, value_type value = value_type()) {
            const size_type currentSize = size();
            if (s == currentSize)
                return;

            if (s < currentSize) {
                erase(begin() + s, *end());
            } else {
                insert(end(), s - currentSize, value);
            }
        }
#endif

#if __cplusplus > 201703L
        size_type remove(const value_type &value) {
            size_type removed = 0;
            for (auto i = begin(); i != end(); ++i) {
                if (*i == value) {
                    i = erase(i);
                    removed += 1;
                }
            }
            return removed;
        }

        template <class _UnaryPredicate> size_type remove_if(_UnaryPredicate p) {
            size_type removed = 0;
            for (auto i = begin(); i != end(); ++i) {
                if (p(*i)) {
                    i = erase(i);
                    removed += 1;
                }
            }
            return removed;
        }
#else
        void remove(const value_type &value) {
            for (auto i = begin(); i != end(); ++i) {
                if (*i == value) {
                    i = erase(i);
                }
            }
        }

        template <class _UnaryPredicate> void remove_if(_UnaryPredicate p) {
            for (auto i = begin(); i != end(); ++i) {
                if (p(*i)) {
                    i = erase(i);
                }
            }
        }

        void splice(const_iterator at, TList &other) {
            iterator i = --other.end();
            while (true) {
                splice(at, other, i);

                if (i == other.begin())
                    break;

                --i;
            }
        }

        void splice(const_iterator at, TList &other, const_iterator sp) {
            TNode_ *current = at.mNode;
            TNode_ *prev = current->mPrev;

            TNode_ *transfer = sp.mNode;
            if (!transfer)
                return;

            if (transfer == other.mBegin) {
                other.mBegin = transfer->mNext;
            } else if (transfer == other.mEnd) {
                other.mEnd = transfer->mPrev;
            } else {
                TNode_ *transferPrev = transfer->mPrev;
                TNode_ *transferNext = transfer->mNext;
                transferPrev->mNext = transferNext;
                transferNext->mPrev = transferPrev;
            }

            transfer->mNext = current;
            transfer->mPrev = prev;

            current->mPrev = transfer;
            prev->mNext = transfer;

            mSize += 1;
            other.mSize -= 1;
        }

        void splice(const_iterator at, TList &other, const_iterator first, const_iterator last) {
            while (first != last) {
                splice(at, other, first++);
            }
        }
#endif

    private:
        node_allocator_t mNodeAllocator;
        size_type mSize;
        TNode_ *mBegin;
        TNode_ *mEnd;
    };

    template <class _T, class _Alloc>
    bool operator==(const TList<_T, _Alloc> &lhs, const TList<_T, _Alloc> &rhs) {
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
    bool operator!=(const TList<_T, _Alloc> &lhs, const TList<_T, _Alloc> &rhs) {
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
    using void_item = void *;
    class TList_pointer_void : public TList<void_item> {};

    template <typename _T> class TList_pointer : public TList_pointer_void {};

}  // namespace JGadget