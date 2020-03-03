#include <thread>
#include <exception>
#include <mutex>
#include <list>
#include <chrono>


template<class T>
class Node {
public:
    explicit Node(T _data) : data(_data), infront(nullptr), behind(nullptr) {}

    std::mutex m;

    void SetInfront(std::shared_ptr<Node> f) {infront = f;}
    void SetBehind(std::shared_ptr<Node> b) {behind = b;}

    std::shared_ptr<Node> GetInfront() const {return infront;}
    std::shared_ptr<Node> GetBehind() const {return behind;}

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
    std::shared_ptr<Node<T>> GetFront() const {
        //TODO: lock properly these functions
        return front;
    }
    // return a pointer to the rear node
    std::shared_ptr<Node<T>> GetBack() const {return back;}

    // adds a data item in front of the first item
    void PushFront(T item) {
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
            front->SetInfront(newNode);
            // and newNode to old front
            newNode->SetBehind(front);
            // finally newNode is at the front of the list
            front = newNode;
        }
    }

    // adds a data item behind the last item
    // TODO: const or not?
    void PushBack(T const item) {
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
            back->SetBehind(newNode);
            // and newNode to old back
            newNode->SetInfront(back);
            // finally newNode is at the back of the list
            back = newNode;
        }
    }

    // removes the first data item
    void PopFront() {
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
            std::shared_ptr<Node<T>> behindNeigh(front->GetBehind());
            std::lock_guard<std::mutex> behindLock(behindNeigh->m);

            // fix connections FRONT TO BACK
            front = behindNeigh;
            behindNeigh->SetInfront(front);

            // TODO: I assume that nothing is pointing to death-row node now so it should die according to make_shared
        }
    }

    // removes the last data item
    void PopBack() {
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
            // TODO: I assume that nothing is pointing to death-row node now so it should die according to make_shared
        }
    }

    // adds a data node before given location in the chain
    void insert(const T item, const std::shared_ptr<Node<T>> locator);

    // erases a data node at a given location in the chain
    void erase(const std::shared_ptr<Node<T>> nodeToKill) {


        while(true) {
            // first lock the node to be removed
            std::lock_guard<std::mutex> eraseLock(nodeToKill->m);

            // check that there is a forward node
            std::shared_ptr<Node<T>> infrontNode(nodeToKill->GetInfront());
            if (!infrontNode) {
                // we are at the front so lock high-level mutex
                std::lock_guard<std::mutex> listLock(m);
            } else {
                // we are not at the front so ATTEMPT to lock infront node
                std::unique_lock<std::mutex> forwardLock(infrontNode->m, std::defer_lock);
                if (!forwardLock.try_lock()) {
                    // if we fail to lock the backward lock, unlock everything and try again
                    // TODO: almost certainly some livelocking going on here, not too serious
                    continue;
                }
            }

            // check that there is a backward node
            std::shared_ptr<Node<T>> backwardNode(nodeToKill->GetBehind());
            if (!backwardNode) {
                // is the queue length 1 (ie high-level is already locked)
                // we are at the back so lock high-level mutex
                if(infrontNode) std::lock_guard<std::mutex> listLock(m);
            } else {
                // we are not at the back so lock backward node
                std::lock_guard<std::mutex> backwardLock(backwardNode->m);
            }

            // we now have all the necessary locks in out possession, so modify data accordingly

            // if we're at front, kill high-level reference
            if(!infrontNode) {
                // !infrontNode == true also in case that we've already killed this node so validate
                if(front == nodeToKill) front = nodeToKill->GetBehind();
            }
            // otherwise rearrange forward neighbour
            else {
                // NOTE: this is stll valid even if we are at the back of the queue
                infrontNode->SetBehind(nodeToKill->GetBehind());
            }

            // if we're at back, kill high-level reference
            if(!backwardNode) {
                // !backwardNode == true also in case that we've already killed this node so validate
                if(back == nodeToKill) back = nodeToKill->GetInfront();
            }
            // otherwise rearrange backward neighbour
            else {
                // NOTE: this is stll valid even if we are at the front of the queue
                backwardNode->SetInfront(nodeToKill->GetInfront());
            }

            // now ensure both refs inside nodeToKill are dead
            nodeToKill->SetBehind() = nullptr;
            nodeToKill->SetInfront() = nullptr;

            // escape the loop as we're done
            break;
        }

    }

    // traverses the list and returns the length
    // TODO: this is VERY expensive and holds the high level lock for ages
    unsigned long GetLength();

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
