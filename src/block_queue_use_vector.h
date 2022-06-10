#ifndef BLOCK_QUEUE_USE_VECTOR_H
#define BLOCK_QUEUE_USE_VECTOR_H

#include <iostream>
// 用vector实现的blockqueue
template <typename T>
class BlockQueue {
    std::mutex              _mutex;
    std::condition_variable _not_full;
    std::condition_variable _not_empty;
    int                     _start;
    int                     _end;
    int                     _capacity;
    std::vector<T>          _vt;

   public:
    BlockQueue(const BlockQueue<T>& other) = delete;
    BlockQueue<T>& operator=(const BlockQueue<T>& other) = delete;
    BlockQueue(int capacity) : _capacity(capacity), _vt(capacity + 1), _start(0), _end(0) {}

    bool isempty() {
        return _end == _start;
    }

    bool isfull() {
        return (_start + _capacity - _end) % (_capacity + 1) == 0;
    }

    int count(){
        return (_start + _capacity - _end) % _capacity;
    }

    void push(const T& e) {
        std::unique_lock<std::mutex> lock(_mutex);
        while (isfull()) {
            // std::cout<<"push is wait"<<std::endl;
            _not_full.wait(lock);
        }

        _vt[_end++] = e;
        _end %= (_capacity + 1);
        lock.unlock();
        _not_empty.notify_one();
    }


    T pop() {
        std::unique_lock<std::mutex> lock(_mutex);
        while (isempty()) {
            // std::cout<<"pop is wait"<<std::endl;
            _not_empty.wait(lock);
        }

        auto res = _vt[_start++];
        _start %= (_capacity + 1);
        lock.unlock();
        _not_full.notify_one();
        return res;
    }

    void notifyAll(){
        std::unique_lock<std::mutex> lock(_mutex);
        _not_full.notify_one();
        _not_empty.notify_one();
        lock.unlock();
    }
};

#endif