#include <thread>
#include <exception>
#include <mutex>
#include <list>
#include <chrono>
#include <map>
#include <string>
#include <tuple>


template<class T>
class Node {
public:
    explicit Node(T _data, bool _direction) : data(_data), right(nullptr), left(nullptr), direction(_direction) {}

    std::mutex m;

    void SetInfront(std::shared_ptr<Node> f) {
        direction ? (right = f) : (left = f);
    }
    void SetBehind(std::shared_ptr<Node> b) {
        direction ? (left = b) : (right = b);
    }

    std::shared_ptr<Node> GetInfront() const {
        return direction ? right : left;
    }
    std::shared_ptr<Node> GetBehind() const {
        return direction ? left : right;
    }

    void SetDirection(bool newDirection) { direction = newDirection; }

    T const getData() const {return data;}

private:
    // pointers to neighbour to front and back
    std::shared_ptr<Node> right;
    std::shared_ptr<Node> left;

    // swappable traverse and locking order
    // true: right = infront; false: left = infront
    bool direction;

    T const data;
};

template<class T>
class ReversibleQueue {
public:
    ReversibleQueue() : front(nullptr), back(nullptr), direction(true) {}

    // TODO: could mutex individual data (front, back); significant benefit? probably not worth it
    mutable std::mutex m;

    // adds a data item to the front of the queue
    void PushFront(T item) {
        // acquire high-level list mutex
        std::lock_guard<std::mutex> listLock(m);


        // create a new node object
        // TODO: is item valid?
        const std::shared_ptr<Node<T>> newNode = std::make_shared<Node<T>>(item,direction);
        // acquire newNode
        std::lock_guard<std::mutex>newNodeLock(newNode->m);


        // is list empty? note don't need to check !back as is always true if !front
        if(!front) {
            // newNode is at the back of the list (as well as front)
            back = newNode;
            // point to itself to signify end of list
            newNode->SetBehind(newNode);
        }
        // otherwise link newNode to its new rear neighbour
        else {
            // acquire low level mutex for old front elem as is written
            std::lock_guard<std::mutex> oldFrontLock(front->m);
            // link old front to newNode
            front->SetInfront(newNode);
            // and newNode to old front
            newNode->SetBehind(front);
        }
        // newNode is at the front of the list
        front = newNode;
        // newNode to itself to signify front of list
        newNode->SetInfront(newNode);
    }

    // adds a data item behind the last item
    void PushBack(T const item) {
        // TODO: is item valid?

        // generate a newNode
        const std::shared_ptr<Node<T>>newNode = std::make_shared<Node<T>>(item,direction);

        // acquire high level list mutex
        // (write to front/back)
        std::lock_guard<std::mutex> listLock(m);
        // acquire low level node mutex
        std::lock_guard<std::mutex> newNodeLock(newNode->m);

        // is list empty? note don't need to check !front as is always true if !back
        if(!back) {
            // newNode is at front of list (as well as back)
            front = newNode;
            newNode->SetInfront(newNode);
        }
        // otherwise link this bad boy to its new in-front neighbour
        else {
            // acquire low level mutex for old back elem as is written
            std::lock_guard<std::mutex> oldBackLock(back->m);
            // safely link old back to newNode
            back->SetBehind(newNode);
            // and newNode to old back
            newNode->SetInfront(back);
        }
        // finally newNode is at the back of the list
        back = newNode;
        newNode->SetBehind(newNode);
    }

    // removes the first data item
    void PopFront() {
        // acquire high level list mutex
        // (write to *f / b* => invalid)
        std::lock_guard<std::mutex> listLock(m);

        // empty list
        if(!front) throw std::logic_error("cannot pop from empty list");
        // acquire death-row node FIRST
        std::lock_guard<std::mutex> eraseLock(front->m);
        // single item in list? => safe to burn the references
        if (front == back) {
            front->SetBehind(nullptr);
            front->SetInfront(nullptr);
            front = nullptr;
            back = nullptr;
        } else {
            // otherwise get neighbour BEHIND this one
            std::shared_ptr<Node<T>> behindNode(front->GetBehind());
            std::lock_guard<std::mutex> behindLock(behindNode->m);

            // modify data
            front->SetBehind(nullptr);
            front->SetInfront(nullptr);
            // behindNode is now at the front
            behindNode->SetInfront(behindNode);
            front = behindNode;
        }

    }

    // removes the last data item
    void PopBack() {
        // acquire high level list mutex
        // (write to *f / b* => invalid)
        std::lock_guard<std::mutex> listLock(m);

        // empty list
        if(!back) throw std::logic_error("cannot pop from empty list");

        // This function requires a locking attempt loop due to it requiring a lock and its forward neighbour
        // we are making the locking attempt on forward neighbours weak in order to remove deadlock states
        while(true) {
            // acquire death-row node FIRST
            std::lock_guard<std::mutex> eraseLock(back->m);
            // single item in list? => safe to burn the references
            if (front == back) {
                back->SetBehind(nullptr);
                back->SetInfront(nullptr);
                front = nullptr;
                back = nullptr;
                break;
            } else {

                // otherwise, attempt to lock the forwardNeighbour
                std::shared_ptr<Node<T>> infrontNode(back->GetInfront());
                std::unique_lock<std::mutex> infrontLock(infrontNode->m, std::defer_lock);
                if (!infrontLock.try_lock()) {
                    // if we fail to lock the forward lock, unlock everything and try again
                    // TODO: almost certainly some livelocking going on here, not too serious
                    continue;
                }

                // we have all the necessary locks, so modify data
                back->SetInfront(nullptr);
                back->SetBehind(nullptr);
                infrontNode->SetBehind(infrontNode);
                back = infrontNode;

                // we done, so leave loop
                break;
            }
        }
    }

    // adds a data node behind the given current thread observer location
    // will throw an error if the observer is looking at the rear node
    void insert(const T item) {

        // NOTE: this operation never requires the ownership of the high-level mutex so multiple can occur simultaneously

        std::shared_ptr<Node<T>> locatorNode(threadLocator[std::this_thread::get_id()]);

        if (!locatorNode) throw std::logic_error("thread not currently observing queue");

        // generate a newNode and acquire it
        const std::shared_ptr<Node<T>> newNode = std::make_shared<Node<T>>(item, direction);
        std::lock_guard<std::mutex> newLock(newNode->m);

        // check if there is a node behind the locatorNode
        std::shared_ptr<Node<T>> behindNode(locatorNode->GetBehind());
        if (behindNode == locatorNode) throw std::domain_error("cannot insert at the back of the queue (use pushBack)");
        // this should NEVER occur, node is dead if we see this error, we have some bad code.
        else if (!behindNode) throw std::logic_error("locatorNode is erased");
        else {
            // lock the behind neighbour
            std::lock_guard<std::mutex> behindLock(behindNode->m);
        }

        // we now hold all relevant locks so modify data
        // back <--> newNode
        // are we at the back
        behindNode->SetInfront(newNode);
        newNode->SetBehind(behindNode);

        // newNode <--> front
        locatorNode->SetBehind(newNode);
        newNode->SetInfront(locatorNode);

    }

    // erases a data node at the thread locator position and then remove the thread locator
    void erase() {

        std::shared_ptr<Node<T>> nodeToKill(threadLocator[std::this_thread::get_id()]);
        if (!nodeToKill) throw std::logic_error("thread not currently observing queue");

        // This function requires a locking attempt loop due to it requiring a lock and its forward neighbour
        // we are making the locking attempt on forward neighbours weak in order to remove deadlock states
        while(true) {
            // check that there is a forward node
            std::shared_ptr<Node<T>> infrontNode(nodeToKill->GetInfront());
            if (infrontNode == nodeToKill) {
                // we are at the front of the queue so just use PopFront (this is a high-level operation
                nodeToKill->m.unlock();
                PopFront();
                threadLocator[std::this_thread::get_id()] = nullptr;
                return;
            }
            // this should not happen
            else if (!infrontNode) throw std::logic_error("locatorNode is already erased");
            else {
                // we are not at the front so ATTEMPT to lock node infront
                std::unique_lock<std::mutex> infrontLock(infrontNode->m, std::defer_lock);
                if (!infrontLock.try_lock()) {
                    // if we fail to lock the forward lock, unlock everything and try again
                    nodeToKill->m.unlock();
                    // give the blocking thread a chance to complete acquire of this/release that node
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    nodeToKill->m.lock();
                    continue;
                }
            }

            // check that there is a node behind
            std::shared_ptr<Node<T>> behindNode(nodeToKill->GetBehind());
            // we can catch this condition when we call erase and issue a PopBack
            if (behindNode == nodeToKill) throw std::domain_error("cannot erase node at the back of the queue (use PopBack)");
            // this definitely should never happen as it should have been caught in the above version, put here for
            // completeness
            if (!behindNode) throw std::logic_error("locatorNode is already erased");

            std::lock_guard<std::mutex> behindLock(behindNode->m);
            // we now have all the necessary locks in out possession, so modify data accordingly
            infrontNode->SetBehind(nodeToKill->GetBehind());
            behindNode->SetInfront(nodeToKill->GetInfront());
            // now kill both refs inside nodeToKill to mark its death
            nodeToKill->SetBehind() = nullptr;
            nodeToKill->SetInfront() = nullptr;
            // kill thread locator
            threadLocator[std::this_thread::get_id()] = nullptr;
            break;
        }

    }

    // changes the access and traverse direction of the queue
    void reverse();


    // traverses the list and returns the length
    // NOTE: by the time this function returns additional nodes could have been added/removed by other threads
    // NOTE: so only serves to be an APPROXIMATION FOR LENGTH
    unsigned long GetLength() const;

private:

    // pointers to rightmost and leftmost nodes
    std::shared_ptr<Node<T>> front;
    std::shared_ptr<Node<T>> back;

    // stores the location in the list each thread is currently holding
    // this ensures that a thread will maintain ownership of a node while "inside" the list
    // this allows arbitrary list traversal of any number of threads.
    // TODO: potential issues can arise from thread acess locations from crossing over eachother
    std::map<std::thread::id, std::shared_ptr<Node<T>>> threadLocator;

    // enforces the entry side and direction of list traversing
    // true: front = front; false: back = front
    bool direction;
};

int main() {
    ReversibleQueue<std::tuple<int,std::string>> testQueue;
    testQueue.PushFront(std::tuple<int,std::string>(1,"tt"));
    testQueue.PushFront(std::tuple<int,std::string>(2,"tt"));
    testQueue.PushFront(std::tuple<int,std::string>(3,"tt"));
    testQueue.PushFront(std::tuple<int,std::string>(4,"tt"));
    testQueue.PushFront(std::tuple<int,std::string>(5,"tt"));
    testQueue.PopBack();
    testQueue.PopBack();
    testQueue.PopBack();
    testQueue.insert(std::tuple<int,std::string>(3,"2"));
    int i =3;
}