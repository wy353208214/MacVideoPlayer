#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>

using std::mutex;
using std::condition_variable;

// 用链表节点实现的blockqueue
template <typename T>
class BlockQueue
{
private:
    struct Node {
        T item;
        Node *next;
    };
    //队列头
    Node *front;
    //队列尾
    Node *rear;
    //当前队列中元素数量
    int items;
    //队列最大数值
    const int qsize;


    mutex mutex_lock;
    condition_variable not_empty;
    condition_variable not_full;

    BlockQueue(){};
public:
    BlockQueue(int size):qsize(size){
        front = rear = nullptr;
        items = 0;
    }
    ~BlockQueue();
    bool pop(T &t);
    bool push(const T &t);
    bool isEmpty();
    bool isFull();
    int count();
    void notifyAll();
};

template <typename T>
BlockQueue<T>::~BlockQueue(){
    Node *temp;
    while (front != 0)
    {
        temp = front;
        front = front->next;
        delete temp;
    }
    items = 0;
}

template <typename T>
bool BlockQueue<T>::isEmpty(){
    return items == 0;
}

template <typename T>
bool BlockQueue<T>::isFull(){
    return items == qsize;
}

template <typename T>
bool BlockQueue<T>::pop(T &t){

    std::unique_lock<mutex> lock(mutex_lock);
    while(isEmpty()){
        // std::cout<<"block empty"<<std::endl;
        not_empty.wait(lock);
    }

    Node *temp = front;
    t = temp->item;
    front = front->next;
    delete temp;
    items--;
    if (isEmpty())
    {
        rear = nullptr;
    }
    lock.unlock();
    not_full.notify_all();
    return true;
}

template <typename T>
bool BlockQueue<T>::push(const T &t){
    
    std::unique_lock<mutex> lock(mutex_lock);
    while(isFull()){
        not_full.wait(lock);
    }

    Node *temp = new Node;
    temp->item = t;
    temp->next = nullptr;
    items++;
    if(front == nullptr){
        front = temp;
    }else {
        rear->next = temp;
    }
    lock.unlock();
    not_empty.notify_all();
    rear = temp;
}

template <typename T>
int BlockQueue<T>::count(){
    return items;
}

template <typename T>
void notifyAll(){
    not_empty.notify_all();
    not_full.notify_all();
}

#endif