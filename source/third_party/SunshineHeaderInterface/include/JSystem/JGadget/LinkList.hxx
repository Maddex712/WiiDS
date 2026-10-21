#pragma once

#include <Dolphin/OS.h>
#include <Dolphin/types.h>

#include <JSystem/JGadget/Allocator.hxx>
#include <JSystem/JGadget/Node.hxx>
#include <JSystem/JKernel/JKRHeap.hxx>
#include <JSystem/initializer_list.hxx>

namespace JGadget {
    class TNodeLinkList {
    public:
        class iterator {
        public:
            iterator(TLinkListNode *node) : mCurrent(node) {}
            iterator(const iterator &iter) : mCurrent(iter.mCurrent) {}

            bool operator==(iterator &rhs) const { return mCurrent == rhs.mCurrent; }
            bool operator!=(iterator &rhs) const { return mCurrent != rhs.mCurrent; }
            TLinkListNode *operator->() const { return mCurrent; }

            TLinkListNode *mCurrent;
        };

        TLinkListNode *CreateNode_(TLinkListNode *prev, TLinkListNode *next, void **item);

        iterator begin() { return iterator(mBegin); }
        iterator end();
        iterator Erase(iterator start, iterator end);
        iterator Insert(iterator iter);
        void Remove(TLinkListNode *node);
        template <typename _T> void Remove_if(_T _p, TNodeLinkList &list);

        size_t mSize;
        TLinkListNode *mBegin;
    };

    template <typename _T, size_t _S> class TLinkList {
    public:
        TLinkListNode *Element_getNode();
        _T *Element_getValue();
    };

    class TSingleNodeLinkList {
    public:
        class iterator {
        public:
            iterator(TSingleLinkListNode *node) : mNode(node) {}
            iterator(const iterator &iter) : mNode(iter.mNode) {}

            bool operator==(const iterator &rhs) const { return mNode == rhs.mNode; }
            bool operator!=(const iterator &rhs) const { return mNode != rhs.mNode; }

            iterator &operator++() {
                mNode = mNode->mNext;
                return *this;
            }

            TSingleLinkListNode *operator->() const { return mNode; }
            TSingleLinkListNode &operator*() { return *mNode; }

            TSingleLinkListNode *mNode;
        };

        void Initialize_();

        iterator Insert(iterator iter, TSingleLinkListNode *node);

        iterator Erase(iterator iter) {
            auto searchIter = begin();
            while (searchIter != end()) {
                TSingleLinkListNode *next = searchIter->mNext;
                if (next == iter.mNode) {
                    searchIter->mNext = iter->mNext;
                    delete iter.mNode;

                    mSize -= 1;
                    return iterator(searchIter->mNext);
                }
                ++searchIter;
            }
            return end();
        }

        iterator Erase(iterator start, iterator end) {
            iterator iter = start;
            while (iter != end) {
                iter = Erase(iter);
            }
            return iterator(iter);
        }

        iterator end();
        iterator begin() { return {mStart}; }

        size_t mSize;
        TSingleLinkListNode *mStart;
        TSingleLinkListNode *mEnd;
    };

    template <typename _T, size_t _S> class TSingleLinkList {
    public:
        class iterator {
        public:
            iterator(TSingleLinkListNode *node) : mCurrent(node) {}
            iterator(const iterator &iter) : mCurrent(iter.mCurrent) {}

            TSingleLinkListNode *mCurrent;
        };
        size_t mSize;
        TSingleLinkList mNode;
    };
}  // namespace JGadget