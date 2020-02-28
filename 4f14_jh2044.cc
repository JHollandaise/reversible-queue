#include <thread>
#include <exception>


template<class T>
class QueueNode {
public:
    explicit QueueNode(T _data) : data(_data) {};

    void setNext(std::shared_ptr<QueueNode> n) next = n;
private:
    T data;
    // points to the QueueNode behind it in the queue
    std::shared_ptr<QueueNode> next;
}

template<class T>
class ThreadsafeQueue {
public:
    // adds a new data item to the back of the queue
    void push(T newItem);

    // removes front queue node and returns a pointer to a const shared object
    // containing the removed node's data
    std::shared_ptr<T> pop();

    // removes front queue node and stores contained data in a refered object
    void pop(T& result);




private:
    std::shared_ptr<QueueNode<T>> front;
    std::shared_ptr<QueueNode<T>> back;
    unsigned int size;


}
