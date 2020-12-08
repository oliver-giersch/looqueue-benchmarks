#ifndef LOO_QUEUE_BENCHMARK_LCRQ_HPP
#define LOO_QUEUE_BENCHMARK_LCRQ_HPP

#include "lcrq_fwd.hpp"
#include "queues/lcr/detail/node.hpp"

namespace lcr {
template <typename T>
queue<T>::queue(std::size_t max_threads) :
  m_hazard_pointers{ max_threads }
{
  auto head = new crq_node_t();
  this->m_head.store(head, std::memory_order_relaxed);
  this->m_tail.store(head, std::memory_order_relaxed);
}

template <typename T>
queue<T>::~queue<T>() noexcept {
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
    throw std::invalid_argument("enqueue element must not be null");
  }

  while (true) {
    auto tail = this->m_hazard_pointers.protect_ptr(this->m_tail.load(ACQ), thread_id, HP_ENQ_TAIL);
    if (tail != this->m_tail.load(RLX)) {
      continue;
    }

    auto next = tail->next.load(ACQ);
    if (next != nullptr) {
      this->m_tail.compare_exchange_strong(tail, next, REL, RLX);
      continue;
    }

    if (tail->enqueue(elem)) {
      break;
    }

    auto crq = new crq_node_t(elem);

    if (tail->cas_next(nullptr, crq)) {
      this->m_tail.compare_exchange_strong(tail, crq, REL, RLX);
      break;
    }

    delete crq;
  }

  this->m_hazard_pointers.clear_one(thread_id, HP_ENQ_TAIL);
}

template <typename T>
typename queue<T>::pointer queue<T>::dequeue(std::size_t thread_id) {
  pointer res;
  while (true) {
    auto head = this->m_hazard_pointers.protect_ptr(this->m_head.load(ACQ), thread_id, HP_DEQ_HEAD);
    if (head != this->m_head.load(RLX)) {
      continue;
    }

    res = head->dequeue();
    if (res != nullptr) {
      break;
    }

    if (head->next.load(RLX) == nullptr) {
      res = nullptr;
      break;
    }

    res = head->dequeue();
    if (res != nullptr) {
      break;
    }

    auto next = head->next.load(ACQ);
    if (this->m_head.compare_exchange_strong(head, next, REL, RLX)) {
      this->m_hazard_pointers.retire(head, thread_id);
    }
  }

  this->m_hazard_pointers.clear_one(thread_id, HP_DEQ_HEAD);
  return res;
}
}

#endif /* LOO_QUEUE_BENCHMARK_LCRQ_HPP */
