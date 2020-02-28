#include <thread>
#include <exception>
#include <mutex>
#include <list>


template<class T>
class Node {
public:
    explicit Node(T _data) : data(_data) {}

    mutable std::mutex m;

    void setPrev(std::shared_ptr<Node> p) {previous = p;}
    void setNext(std::shared_ptr<Node> n) {next = n;}

    std::shared_ptr<Node> getPrev() const {return previous;}
    std::shared_ptr<Node> getNext() const {return next;}

    T getData() const {return data;}

private:
    std::shared_ptr<Node> previous;
    std::shared_ptr<Node> next;
    T data;
};

template<class T>
class List {
public:
    List();

    // return a pointer to the front node
    std::shared_ptr<Node<T>> getFront() const;
    // return
    std::shared_ptr<Node<T>> getBack() const;

    void push_front();
    void push_back();

    void pop_front();
    void pop_back();

    // adds a data node before given location in the chain
    void insert(T data, std::shared_ptr<Node<T>> locator);

    // erases a data node at a given location in the chain
    void erase(std::shared_ptr<Node<T>> locator);



private:
    std::shared_ptr<Node<T>> front;
    std::shared_ptr<Node<T>> back;
};

template<class T> 
class ReversibleQueue {
public:
    ReversibleQueue() : direction(true) {};
    
    // returns the length of the data deque
    auto getSize() const;

    // adds a data item to the back of the queue
    void push()

    // removes a data item from the front of the queue
    void pop();

    // swaps the direction of the queue
    void reverse();

    // removes the element at the given position
    auto erase(auto pos);

    // traverse the queue in a given direction
    void move()
    

private:
    // standard list to contain data
    std::list<T> data;
    // defines the queue direction 
    // true: list front=queue front
    // false: list front=queue back
    bool direction;

};