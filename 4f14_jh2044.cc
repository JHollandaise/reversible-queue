#include <thread>
#include <exception>
#include <mutex>
#include <list>


template<class T>
class Node {
public:
    explicit Node(T _data) : data(_data), infront(nullptr), behind(nullptr) {}

    std::mutex m;

    void setInfront(std::shared_ptr<Node> f) {infront = f;}
    void setBehind(std::shared_ptr<Node> b) {behind = b;}

    std::shared_ptr<Node> getInfront() const {return infront;}
    std::shared_ptr<Node> getBehind() const {return behind;}

    T const getData() const {return data;}

private:
    // pointers to neighbour in-front and behind
    std::shared_ptr<Node> infront;
    std::shared_ptr<Node> behind;
    T const data;
};

template<class T>
class List {
public:
    List() : front(nullptr), back(nullptr) {}

    // TODO: could mutex individual data (front, back, size), significant benefit?
    mutable std::mutex m;

    // return a pointer to the front node
    std::shared_ptr<Node<T>> getFront() const {return front;}
    // return a pointer to the rear node
    std::shared_ptr<Node<T>> getBack() const {return back;}

    // adds a data item in front of the first item
    void push_front(T item) {
        // TODO: is item valid?
        std::shared_ptr<Node<T>>newNode = std::make_shared<Node<T>>(item);

        // acquire high level list mutex
        // (write to *f and *b => invalid)
        std::lock_guard<std::mutex> listLock(m);
        // acquire low level node mutex as is not yet in list so may have
        // invalid pointers
        std::lock_guard<std::mutex>newNodeLock(newNode->m);


        // is list empty?
        if(!front) {
            // node is at front and back of list
            front = newNode;
            back = newNode;
        }
        // otherwise link this bad boy to its new rear neighbour
        else {
            // acquire low level mutex for old front elem as is written
            std::lock_guard<std::mutex> oldFrontLock(front->m);
            // safely link old front to newNode
            front->setInfront(newNode);
            // and newNode to old front
            newNode->setBehind(front);
            // finally newNode is at the front of the list
            front = newNode;
        }
    }

    // adds a data item behind the last item
    // TODO: const or not?
    void push_back(T const item) {
        // TODO: is item valid?
        const std::shared_ptr<Node<T>>newNode = std::make_shared<Node<T>>(item);

        // acquire high level list mutex
        // (write to *f and *b => invalid)
        std::lock_guard<std::mutex> listLock(m);
        // acquire low level node mutex as is not yet in list so may have
        // invalid pointers
        std::lock_guard<std::mutex> newNodeLock(newNode->m);

        // is list empty?
        if(!back) {
            // node is at front and back of list
            front = newNode;
            back = newNode;
        }
        // otherwise link this bad boy to its new in-front neighbour
        else {
            // acquire low level mutex for old back elem as is written
            std::lock_guard<std::mutex> oldBackLock(back->m);
            // safely link old back to newNode
            back->setBehind(newNode);
            // and newNode to old back
            newNode->setInfront(back);
            // finally newNode is at the back of the list
            back = newNode;
        }
    }

    // removes the first data item
    void pop_front() {
        // acquire high level list mutex
        // (write to *f / b* => invalid)
        std::lock_guard<std::mutex> listLock(m);

        // empty queue
        if(!front) throw std::logic_error("cannot pop from empty list");
        // single item in list? => safe to burn the references
        else if (front == back) {
            // TODO: check don't need to acquire the low-level mutex here as is alone
            front = nullptr;
            back = nullptr;
        }
            // otherwise need to then acquire the relevant low-level
        else {
            // acquire death-row node FIRST
            std::lock_guard<Node<T>> popLock(front->m);
            // THEN get neighbour BEHIND this one
            std::shared_ptr<Node<T>> behindNeigh(front->getBehind());
            std::lock_guard<std::mutex> behindLock(behindNeigh->m);

            // fix connections FRONT TO BACK
            front = behindNeigh;
            behindNeigh->setInfront(front);

            // TODO: I assume that nothing is pointing to death-row node now so it should die according to make_shared
        }
    }

    // removes the last data item
    void pop_back() {
        // acquire high level list mutex
        // (write to *f / b* => invalid)
        std::lock_guard<std::mutex> listLock(m);

        // empty queue
        if(!back) throw std::logic_error("cannot pop from empty list");
        // single item in list? => safe to burn the references
        else if (front == back) {
            // TODO: check don't need to acquire the low-level mutex here as is alone
            front = nullptr;
            back = nullptr;
        }
        // otherwise need to then acquire the relevant low-level
        else {
            // acquire death-row node AND INFRONT FIRST
            std::unique_lock<Node<T>> popLock(front->m, std::defer_lock);
            std::unique_lock<Node<T>> infrontLock(front)
            // THEN get neighbour BEHIND this one
            std::shared_ptr<Node<T>> behindNeigh(front->getBehind());
            std::lock_guard<std::mutex> behindLock(behindNeigh->m);

            // fix connections FRONT TO BACK
            front = behindNeigh;
            behindNeigh->setInfront(front);

            // TODO: I assume that nothing is pointing to death-row node now so it should die according to make_shared
        }
    }

    // adds a data node before given location in the chain
    void insert(T data, std::shared_ptr<Node<T>> locator);

    // erases a data node at a given location in the chain
    void erase(std::shared_ptr<Node<T>> locator);

    // traverses the list and returns the length
    // TODO: this is VERY expensive and holds the high level lock for ages
    unsigned long get_length();

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
    void push();

    // removes a data item from the front of the queue
    void pop();

    // swaps the direction of the queue
    void reverse();

    // removes the element at the given position
    auto erase(auto pos);

    // traverse the queue in a given direction
    void move();


private:
    // standard list to contain data
    std::list<T> data;
    // defines the queue direction
    // true: list front=queue front
    // false: list front=queue back
    bool direction;
    size_t size;

};
