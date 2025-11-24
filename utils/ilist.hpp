#pragma once
#include <std.hpp>

namespace zh_async
{
    //双向链表，且头节点的前向节点就是尾节点
    struct ListHead
    {
        struct ListNode
        {
            ListNode()noexcept: listNext(nullptr),listPrev(nullptr){}
            friend struct ListHead;

        private:
            ListNode *listNext;
            ListNode *listPrev;
        };

        struct NodeType: ListNode
        {
            NodeType() = default;
            NodeType(NodeType &&) = delete;

            ~NodeType()noexcept { erase_from_parent(); }

             protected:
             //删除当前节点
            void erase_from_parent()
            {
                if(this->listNext)
                {
                    auto listPrev = this->listPrev;
                    auto listNext = this->listNext;
                    listPrev->listNext = listNext;
                    listNext->listPrev = listPrev;
                    this->listNext = nullptr;
                    this->listPrev = nullptr;
                }
            }
        };

        //头插法
        void doPushFront(ListNode *node)noexcept
        {
            node->listNext = root.listNext;
            node->listPrev = &root;
            root.listNext = node;
            node->listNext->listPrev = node;
        }

        //尾插法
        void doPushBack(ListNode *node)
        {
            node->listNext = &root;
            node->listPrev = root.listPrev;
            root.listPrev = node;
            node->listPrev->listNext = node;    
        }

        //在 pivot 后面插入节点
        void doInsertAfter(ListNode *pivot,ListNode *node)
        {
            node->listNext = pivot->listNext;
            node->listPrev = pivot;
            pivot->listNext = node;
            node->listPrev->listNext = node;
        }

        //在 pivot 前面插入节点
        void doInsertBefore(ListNode *pivot, ListNode *node) noexcept 
        {
            node->listNext = pivot;
            node->listPrev = pivot->listPrev;
            pivot->listPrev = node;
            node->listPrev->listNext = node;
        }

        //删除节点
        void doErase(ListNode *node)noexcept
        {
            node->listNext->listPrev = node->listPrev;
            node->listPrev->listNext = node->listNext;
            node->listNext = nullptr;
            node->listPrev = nullptr;
        }

        ListNode *doFront()const noexcept { return root.listNext; }

        ListNode *doBack()const noexcept { return root.listPrev; }

        bool doEmpty()const noexcept { return root.listNext == nullptr; }

        //删除首元节点
        ListNode* doPopFront() noexcept
        {
            auto node = root.listNext;
            if(node != &root)
            {
                node->listNext->listPrev = &root;
                root.listNext = node->listNext;
                node->listNext = nullptr;
                node->listPrev = nullptr;
            }else{
                node = nullptr;
            }
            return node;
        }

        //删除尾节点
        ListNode *doPopBack()noexcept
        {
            auto node = root.listPrev;
            if (node != &root) {
            node->listNext->listPrev = node->listPrev;
            node->listPrev->listNext = node->listNext;
            node->listNext = nullptr;
            node->listPrev = nullptr;
        } else {
            node = nullptr;
        }
        return node;
        }

        void doClear()
        {
            for(ListNode *current = root.listNext,*next;current != &root; current = next)
            {
                next = current->listNext;
                current->listNext = nullptr;
                current->listPrev = nullptr;
            }
            root.listNext = root.listPrev = &root;
        }

        //迭代函数
        static void doIterNext(ListNode *&current) noexcept
            { current = current->listNext; }

        static void doIterPrev(ListNode *&current) noexcept 
            {current = current->listPrev;}

        ListNode *doIterBegin()const noexcept { return root.listNext; }

        ListNode *doIterEnd()const noexcept { return root.listPrev; }

        ListHead()noexcept : root() { root.listNext = root.listPrev = &root;}

        ListHead(ListHead &&) = delete;

        ~ListHead()noexcept { doClear(); }

       private:
        ListNode root;
    
    };

    //侵入式链表
    template <class Value>
struct IntrusiveList : private ListHead {
    using ListHead::NodeType;

    IntrusiveList() noexcept {
        static_assert(
            std::is_base_of_v<NodeType, Value>,
            "Value type must be derived from IntrusiveList<Value>::NodeType");
    }

    struct iterator {
    private:
        ListNode *node;

        explicit iterator(ListNode *node) noexcept : node(node) {}

        friend IntrusiveList;

    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = Value;
        using difference_type = std::ptrdiff_t;
        using pointer = Value *;
        using reference = Value &;

        iterator() noexcept : node(nullptr) {}

        explicit iterator(Value &value) noexcept
            : node(&static_cast<ListNode &>(value)) {}

        Value &operator*() const noexcept {
            return *static_cast<Value *>(node);
        }

        Value *operator->() const noexcept {
            return static_cast<Value *>(node);
        }

        iterator &operator++() noexcept {
            ListHead::doIterNext(node);
            return *this;
        }

        iterator &operator--() noexcept {
            ListHead::doIterPrev(node);
            return *this;
        }

        iterator operator++(int) noexcept {
            auto copy = *this;
            ListHead::doIterNext(node);
            return copy;
        }

        iterator operator--(int) noexcept {
            auto copy = *this;
            ListHead::doIterPrev(node);
            return copy;
        }

        bool operator==(iterator const &other) const noexcept {
            return node == other.node;
        }

        bool operator!=(iterator const &other) const noexcept {
            return node != other.node;
        }
    };

    using const_iterator = iterator;

    iterator begin() const noexcept {
        return iterator(ListHead::doIterBegin());
    }

    iterator end() const noexcept {
        return iterator(ListHead::doIterEnd());
    }

    void push_front(Value &value) noexcept {
        doPushFront(&static_cast<ListNode &>(value));
    }

    void push_back(Value &value) noexcept {
        doPushBack(&static_cast<ListNode &>(value));
    }

    void insert_after(Value &pivot, Value &value) noexcept {
        doInsertAfter(&static_cast<ListNode &>(pivot),
                      &static_cast<ListNode &>(value));
    }

    void insert_before(Value &pivot, Value &value) noexcept {
        doInsertBefore(&static_cast<ListNode &>(pivot),
                       &static_cast<ListNode &>(value));
    }

    void erase(Value &value) noexcept {
        doErase(&static_cast<ListNode &>(value));
    }

    bool empty() const noexcept {
        return doEmpty();
    }

    Value &front() const noexcept {
        return static_cast<Value &>(*doFront());
    }

    Value &back() const noexcept {
        return static_cast<Value &>(*doBack());
    }

    Value *pop_front() noexcept {
        auto node = doPopFront();
        return node ? static_cast<Value *>(node) : nullptr;
    }

    Value *pop_back() noexcept {
        auto node = doPopBack();
        return node ? static_cast<Value *>(node) : nullptr;
    }

    void clear() {
        doClear();
    }
};
}