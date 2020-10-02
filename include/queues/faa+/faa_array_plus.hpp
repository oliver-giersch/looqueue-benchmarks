#ifndef LOO_QUEUE_BENCHMARK_FAA_ARRAY_PLUS_HPP
#define LOO_QUEUE_BENCHMARK_FAA_ARRAY_PLUS_HPP

#include "faa_array_plus_fwd.hpp"
#include "queues/faa+/detail/node.hpp"

#if defined(__GNUG__) || defined(__clang__) || defined(__INTEL_COMPILER)
#define   likely(cond) __builtin_expect ((cond), 1)
#define unlikely(cond) __builtin_expect ((cond), 0)
#else
#define   likely(cond) cond
#define unlikely(cond) cond
#endif

namespace faa_plus {
template <typename T>
queue<T>::queue(std::size_t max_threads) : m_hazard_ptrs{ max_threads, 1 } {
  auto sentinel = new node_t();
  std::atomic_init(&this->m_head, sentinel);
  std::atomic_init(&this->m_tail, sentinel);
}

template <typename T>
queue<T>::~queue() noexcept {
  auto curr = this->m_head.load(std::memory_order_relaxed);
  while (curr != nullptr) {
    const auto next = curr->next.load(std::memory_order_relaxed);
    delete curr;
    curr = next;
  }
}

template <typename T>
void queue<T>::enqueue(queue::pointer elem, std::size_t thread_id) {
  if (elem == nullptr) {
    throw std::invalid_argument("enqueue element must not be nullptr");
  }

  while (true) {
    const auto tail = this->m_hazard_ptrs.protect_ptr(this->m_tail.load(), thread_id, HP_ENQ_TAIL);
    if (unlikely((tail != this->m_tail.load()))) {
      continue;
    }

    const auto idx = tail->enq_idx.fetch_add(1);
    if (likely(idx < NODE_SIZE)) {
      // ** fast path ** write (CAS) pointer directly into the reserved slot
      if (likely(tail->cas_slot_at(idx, nullptr, elem))) {
        break;
      }

      continue;
    } else {
      // ** slow path ** append new tail node or update the tail pointer
      if (tail != this->m_tail.load()) {
        continue;
      }

      const auto next = tail->next.load();
      if (next == nullptr) {
        auto node = new node_t(elem);
        if (tail->cas_next(nullptr, node)) {
          this->cas_tail(tail, node);
          break;
        }

        delete node;
      } else {
        this->cas_tail(tail, next);
      }
    }
  }

  this->m_hazard_ptrs.clear_one(thread_id, HP_ENQ_TAIL);
}

template <typename T>
typename queue<T>::pointer queue<T>::dequeue(std::size_t thread_id) {
  pointer res = nullptr;
  while (true) {
    // acquire hazard pointer for head node
    const auto head = this->m_hazard_ptrs.protect_ptr(this->m_head.load(), thread_id, HP_DEQ_HEAD);
    if (head != this->m_head.load()) {
      continue;
    }

    // prevent incrementing dequeue index in case the queue is empty (original empty check)
    if (head->deq_idx.load() >= head->enq_idx.load() && head->next.load() == nullptr) {
      break;
    }

    /**
    // perform light-weight empty check
    if (unlikely((head->deq_idx.fetch_add(0) >= NODE_SIZE))) {
      const auto next = head->next.load();
      if (next == nullptr) {
        break;
      }
    }*/

    const auto idx = head->deq_idx.fetch_add(1);
    if (likely(idx < NODE_SIZE)) {
      // ** fast path ** read the pointer from the reserved slot
      res = head->slots[idx].ptr.exchange(reinterpret_cast<pointer>(TAKEN));
      if (likely(res != nullptr)) {
        break;
      }

      /* post-fix empty check
      // perform full empty check if no pointer was written in the reserved slot
      const auto enq_idx = head->enq_idx.load();
      if (idx > enq_idx && head->next.load() == nullptr) {
        break;
      }*/

      // abandon the slot and attempt to dequeue from another slot
      continue;
    } else {
      // ** slow path ** advance the head pointer to the next node
      const auto next = head->next.load();
      if (next == nullptr) {
        break;
      }

      if (this->cas_head(head, next)) {
        this->m_hazard_ptrs.retire(head, thread_id);
      }

      continue;
    }
  }

  this->m_hazard_ptrs.clear_one(thread_id, HP_DEQ_HEAD);
  return res;
}

template <typename T>
bool queue<T>::cas_head(queue::node_t* expected, queue::node_t* desired) {
  return this->m_head.compare_exchange_strong(expected, desired);
}

template <typename T>
bool queue<T>::cas_tail(queue::node_t* expected, queue::node_t* desired) {
  return this->m_tail.compare_exchange_strong(expected, desired);
}
}

#endif /* LOO_QUEUE_BENCHMARK_FAA_ARRAY_HPP */