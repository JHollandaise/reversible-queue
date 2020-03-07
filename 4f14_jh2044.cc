#include <thread>
#include <exception>
#include <mutex>
#include <list>
#include <chrono>
#include <map>
#include <string>
#include <tuple>
#include <iostream>
#include <random>


template<class T>
class Node {
public:
    explicit Node(T _data, bool _direction) : data(_data), right(nullptr), left(nullptr), direction(_direction) {}

    std::mutex m;

    void SetInfront(std::shared_ptr<Node> f) {
        direction ? (left = f) : (right = f);
    }
    void SetBehind(std::shared_ptr<Node> b) {
        direction ? (right = b) : (left = b);
    }

    std::shared_ptr<Node> GetInfront() const {
        return direction ? left : right;
    }
    std::shared_ptr<Node> GetBehind() const {
        return direction ? right : left;
    }

    void SetDirection(bool newDirection) { direction = newDirection; }

    const T data;

private:
    // pointers to neighbour to front and back
    std::shared_ptr<Node> right;
    std::shared_ptr<Node> left;

    // swappable traverse and locking order
    // true: right = infront; false: left = infront
    bool direction;
};

template<class T>
class ReversibleQueue {
public:
    ReversibleQueue() : front(nullptr), back(nullptr), direction(true) {}

    // TODO: could mutex individual data (front, back); significant benefit? probably not worth it
    mutable std::mutex m;

    // adds a data item to the front of the queue [ O(1) ]
    void PushFront(const T item) {
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

    // adds a data item behind the last item [ O(1) ]
    void PushBack(const T item) {
        // TODO: is item valid?

        // acquire high level list mutex
        // (write to front/back)
        std::lock_guard<std::mutex> listLock(m);

        // generate a newNode
        const std::shared_ptr<Node<T>>newNode = std::make_shared<Node<T>>(item,direction);
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

    // removes the first data item [ O(1) ]
    void PopFront() {
        // acquire high level list mutex
        // (write to *f / b* => invalid)
        std::lock_guard<std::mutex> queueLock(m);

        // empty list
        if(!front) throw std::domain_error("PopFront: cannot pop from empty list");
        // acquire death-row node FIRST
        std::lock_guard<std::mutex> frontLock(front->m);
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

    // removes the last data item [ O(n) ]
    void PopBack() {
        // acquire high level list mutex
        // (write to *f / b* => invalid)
        std::lock_guard<std::mutex> listLock(m);

        // empty list
        if(!back) throw std::domain_error("PopBack: cannot pop from empty list");

        // due to the requirement to propagate locks BACKWARDS ONLY, this must move through the list from front to
        // back in order to ensure a valid lock sequence
        std::unique_lock<std::mutex>eraseLock(back->m);
        while(true) {
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
                    eraseLock.unlock();
                    eraseLock.lock();
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

    // adds a data node BEHIND the given current thread observer location
    // will throw an error if the observer is looking at the rear node
    void Insert(const T item) {

        // NOTE: this operation never requires the ownership of the high-level mutex so multiple can occur simultaneously

        std::shared_ptr<Node<T>> locatorNode(threadLocator[std::this_thread::get_id()]);

        if (!locatorNode) throw std::logic_error("Insert: thread not currently observing queue");

        // generate a newNode and acquire it
        const std::shared_ptr<Node<T>> newNode = std::make_shared<Node<T>>(item, direction);
        std::lock_guard<std::mutex> newLock(newNode->m);

        // check if there is a node behind the locatorNode
        std::shared_ptr<Node<T>> behindNode(locatorNode->GetBehind());
        if (behindNode == locatorNode) throw std::domain_error("Insert: cannot insert at the back of the queue (use pushBack)");
        // this should NEVER occur, node is dead if we see this error, we have some bad code.
        else if (!behindNode) throw std::logic_error("Insert: locatorNode is erased");
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

    // erases a data node BEHIND the thread locator position
    void Erase() {

        std::shared_ptr<Node<T>> nodeToKill(threadLocator[std::this_thread::get_id()]);
        if (!nodeToKill) throw std::logic_error("Erase: thread not currently observing queue");

        // This function requires a locking attempt loop due to it requiring a lock and its forward neighbour
        // we are making the locking attempt on forward neighbours weak in order to remove deadlock states
        while(true) {
            // check that there is a forward node
            std::shared_ptr<Node<T>> infrontNode(nodeToKill->GetInfront());
            if (infrontNode == nodeToKill) {
                // we are at the front of the queue so just use PopFront (this is a high-level operation)
                threadLocator[std::this_thread::get_id()] = nullptr;
                nodeToKill->m.unlock();
                PopFront();
                return;
            }
            // this should not happen
            else if (!infrontNode) throw std::logic_error("Erase: locatorNode is already erased");
            else {
                // we are not at the front so ATTEMPT to lock node infront
                std::unique_lock<std::mutex> infrontLock(infrontNode->m, std::defer_lock);
                if (!infrontLock.try_lock()) {
                    // if we fail to lock the forward lock, unlock everything and try again
                    nodeToKill->m.unlock();
                    // give the blocking thread a chance to complete acquire of this/release that node
                    nodeToKill->m.lock();
                    continue;
                }
            }

            // check that there is a node behind
            std::shared_ptr<Node<T>> behindNode(nodeToKill->GetBehind());
            // we can catch this condition when we call erase and issue a PopBack
            if (behindNode == nodeToKill) throw std::domain_error("Erase: cannot erase node at the back of the queue (use PopBack)");
            // this definitely should never happen as it should have been caught above, put here for completeness
            if (!behindNode) throw std::logic_error("Erase: locatorNode is already erased");

            std::lock_guard<std::mutex> behindLock(behindNode->m);
            // we now have all the necessary locks in out possession, so modify data accordingly
            infrontNode->SetBehind(nodeToKill->GetBehind());
            behindNode->SetInfront(nodeToKill->GetInfront());
            // now kill both refs inside nodeToKill to mark its death
            nodeToKill->SetBehind(nullptr);
            nodeToKill->SetInfront(nullptr);
            // kill thread locator
            threadLocator[std::this_thread::get_id()] = nullptr;
            break;
        }

    }

    // set the thread to observe the front of the queue
    void GoToFront() const {
        std::shared_ptr<Node<T>> currentNode (threadLocator[std::this_thread::get_id()]);
        if(currentNode) {
            currentNode->m.unlock();
        }

        // acquire high level access
        std::lock_guard<std::mutex> queueLock(m);
        threadLocator[std::this_thread::get_id()] = back;
        if(!back) throw std::domain_error("GoToBack: queue empty");
        back->m.lock();
    }

    // moves the observed node to the one in front of current, throws an exception if at the front already
    void MoveForward() const {
        std::shared_ptr<Node<T>> currentNode (threadLocator[std::this_thread::get_id()]);

        if(!currentNode) throw std::logic_error("MoveForward: thread not currently observing the queue");

        // perform a weak lock attempt to acquire node infront
        while(true) {
            std::shared_ptr<Node<T>> infrontNode (currentNode->GetInfront());
            if(!infrontNode) throw std::logic_error("observed node is erased");

            if(infrontNode == currentNode) {
                // we are at the end so release observer
                currentNode->m.unlock();
                threadLocator[std::this_thread::get_id()] = nullptr;
                throw std::domain_error("MoveForward: current observed node at front of queue");
            }

            if(!infrontNode->m.try_lock()) {
                // unlock everything and try again
                currentNode->m.unlock();
                // give the blocking thread a chance to complete acquire of this/release that node
                currentNode->m.lock();
                continue;
            }

            // release lock on this node
            currentNode->m.unlock();
            // now set observer to the infront node
            threadLocator[std::this_thread::get_id()] = infrontNode;
            break;
        }
    }

    // unlocks and stops observing a node
    void ClearObserver() const {
        std::shared_ptr<Node<T>> currentNode (threadLocator[std::this_thread::get_id()]);
        if(currentNode) {
            currentNode->m.unlock();
            threadLocator[std::this_thread::get_id()] = nullptr;
        }
    }

    // changes the access and traverse direction of the queue
    void Reverse() {
        // acquire high-level control
        std::lock_guard<std::mutex> queueLock(m);

        // queue has at least one item
        if(back) {
            // iterate through queue items from back and change direction
            std::shared_ptr<Node<T>> currentNode(back);
            currentNode->m.lock();
            std::shared_ptr<Node<T>> infrontNode;
            while(true) {
                // perform a weak lock attempt to acquire node infront
                while(true) {
                    infrontNode = currentNode->GetInfront();
                    if(!infrontNode) throw std::logic_error("Reverse: observed node is erased");

                    // we're at the front so no need to lock
                    if(infrontNode == currentNode) {
                        break;
                    }
                    if(!infrontNode->m.try_lock()) {
                        // unlock everything and try again
                        currentNode->m.unlock();
                        // give the blocking thread a chance to complete acquire of this/release that node
                        currentNode->m.lock();
                        continue;
                    }
                    break;
                }
                currentNode->SetDirection(!direction);
                if(infrontNode==currentNode) {
                    currentNode->m.unlock();
                    break;
                }
                currentNode->m.unlock();
                // TODO: this is an unsafe operation!!!
                currentNode = infrontNode;
            }
            direction = !direction;
            std::shared_ptr<Node<T>> temp (back);
            back = front;
            front = temp;
        }


    }

    // returns the data contained in the currently observed node
    T GetData() const {
        // acquire high-level locks
        std::shared_ptr<Node<T>> node (threadLocator[std::this_thread::get_id()]);
        if (!node) throw std::logic_error("GetData: thread not currently observing the queue");
        return node->data;
    }

    // initialises a queue observer for the given thread
    void InitObserver() {
        // acquire queue lock
        std::lock_guard<std::mutex> queueLock(m);
        threadLocator[std::this_thread::get_id()] = nullptr;

    }

private:

    // pointers to rightmost and leftmost nodes
    std::shared_ptr<Node<T>> front;
    std::shared_ptr<Node<T>> back;

    // stores the location in the queue each thread is currently holding
    // this ensures that a thread will maintain ownership of a node while "inside" the queue
    // this allows forward queue traversal of any number of threads.
    mutable std::map<std::thread::id, std::shared_ptr<Node<T>>> threadLocator;

    // enforces the entry side and direction of list traversing
    // true: node rhs = infront; false: node rhs = behind
    bool direction;
};

void QueueReverser(ReversibleQueue<std::tuple<int, std::string>> &queue) {
    // reverses the direction of the queue, then prints out the sum of the numerical entries
    // returns when the queue is empty

    queue.InitObserver();
    while (true) {
        queue.ClearObserver();
        // reverse queue direction
        queue.Reverse();

        try {
            queue.GoToBack();
        }
        catch (const std::domain_error&) {
            // the queue is empty
            break;
        }
        // sum the number in entries and print
        long sum(0);
        while(true) {
            std::tuple<int, std::string> data(queue.GetData());

            sum += std::get<0>(data);

            try {
                queue.MoveForward();
            }
                // we have reached the end
            catch (const std::domain_error&) {
                break;
            }

        }
        std::cout << "\n" << sum << "\n";

        // small delay to preven stdout spam
//        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

}

void QueuePrinter(ReversibleQueue<std::tuple<int, std::string>> &queue) {
    // continually prints the sequence of nodes currently in the queue, from back to front
    // returns when the queue is empty

    queue.InitObserver();

    while (true) {
        try {
            queue.GoToBack();
        }
        catch (const std::domain_error&) {
            // the queue is empty
            break;
        }
        while(true) {
            std::tuple<int, std::string> data(queue.GetData());

            // print out the entries in the queue
            std::cout << std::get<0>(data) << " " << std::get<1>(data) << " | ";

            try {
                queue.MoveForward();
            }
            // we have reached the end of the queue
            catch (const std::domain_error&) {
                break;
            }

        }
        std::cout << "\n";
        // small delay to prevent stdout spam
//        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void QueueEraser(ReversibleQueue<std::tuple<int, std::string>> &queue, int queueLength) {
    // continually selects a random element in the queue to remove then waits 0.2 seconds
    // returns when the queue is empty

    queue.InitObserver();

    std::random_device rd;
    std::default_random_engine e{rd()};
    while (true) {
        try {
            queue.GoToBack();
        }
        catch (const std::domain_error&) {
            // the queue is empty
            break;
        }
        std::uniform_int_distribution<int> distDelete{0, queueLength-1};
        int toDelete(distDelete(e));
        for(int i = 0; i < queueLength; i++) {
            if (i==toDelete) {
                try {
                    queue.Erase();
                    break;
                }
                catch (const std::domain_error&) {
                    queue.ClearObserver();
                    queue.PopBack();
                    break;
                }

            }
            try {
                queue.MoveForward();
            }
                // we have reached the end of the queue
            catch (const std::domain_error&) {
                break;
            }

        }
        queueLength -= 1;
        queue.ClearObserver();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

}

int main() {

    ReversibleQueue<std::tuple<int, std::string>> queue;

    const int queueLength(80);

    std::random_device rd;
    std::default_random_engine e{rd()};
    std::uniform_int_distribution<int> distChar{0, 25};
    std::uniform_int_distribution<int> distLen{3, 7};
    std::uniform_int_distribution<int> distNum{0, 255};

    // populate queue from rear end
    for (int i = 0; i < queueLength; i++) {
        std::string word;

        int num = distNum(e);

        for (int j = 0; j < distLen(e); j++) {
            word += static_cast<char>('a' + distChar(e));
        }
        queue.PushBack(std::tuple<int, std::string> (num, word));
    }


    std::thread t1(QueueReverser, std::ref(queue));
    std::thread t2(QueuePrinter, std::ref(queue));
    std::thread t3(QueueEraser, std::ref(queue), queueLength);
    t1.join();
    t2.join();
    t3.join();


}